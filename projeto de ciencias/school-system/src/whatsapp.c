/* Canal WhatsApp: entrega, identificacao do usuario e comandos escolares. */
#include "whatsapp.h"
#include "anonymous_enrollment.h"
#include "classes.h"
#include "ai.h"
#include "database.h"
#include "logger.h"
#include "tasks.h"
#include "users.h"
#include "validation.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(WHATSAPP_USE_CURL)
#include <curl/curl.h>
#endif

int whatsapp_send_via_api(const char *phone, const char *text);

typedef struct {
    int64_t teacher_id, class_id;
    char subject[64], title[121], description[1024], due_date[11];
} PendingTask;

typedef struct {
    int64_t user_id, task_id;
    PendingTask task;
} PendingTaskEdit;

typedef struct {
    int64_t user_id, notice_id, class_id;
    char title[121], content[2001];
} PendingNoticeEdit;

/* Interpreta a turma indicada por um professor ou em uma consulta escolar. */
static int class_mentioned(const char *body, Class *classroom);
static int normalize_class_reference(const char *input, char *out, size_t cap);
static int student_requests_tasks(const char *text);
static int student_requests_notices(const char *text);
static int teacher_whatsapp_message(const char *sender_key, int64_t user_id,
                                    const char *body, char *reply, size_t cap);
static int resolve_class_reference(const char *text, Class *selected,
                                   char *options, size_t options_cap);
static int looks_like_class_reference(const char *text);


int whatsapp_init(void)
{
#if defined(WHATSAPP_USE_CURL)
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return ERROR_NETWORK;
#endif
    log_info("whatsapp: modulo inicializado");
    return SUCCESS;
}

void whatsapp_shutdown(void)
{
#if defined(WHATSAPP_USE_CURL)
    curl_global_cleanup();
#endif
}

int whatsapp_send_message(const char *phone, const char *text)
{
    if (!phone || !validation_is_valid_phone(phone)) return ERROR_INVALID_PHONE;
    if (!text || !*text) return ERROR_INVALID_INPUT;
    if (strlen(text) > 4000) return ERROR_TOO_LONG;
#if defined(WHATSAPP_USE_CURL)
    int rc = whatsapp_send_via_api(phone, text);
    if (rc != SUCCESS) log_warn("whatsapp: envio falhou (%d)", rc);
    return rc;
#else
    log_info("whatsapp: envio local para %s len=%zu", phone, strlen(text));
    return SUCCESS;
#endif
}

static int equal_ci(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++)) return 0;
    }
    return *a == '\0' && *b == '\0';
}

static int prefix_ci(const char *text, const char *prefix)
{
    while (*prefix) {
        if (!*text || tolower((unsigned char)*text++) != tolower((unsigned char)*prefix++)) return 0;
    }
    return 1;
}

/* Lê comandos como "EDITAR TAREFA 12 | ..." sem aceitar sufixos no ID. */
static int command_target_id(const char *body, const char *prefix,
                             int64_t *id_out, const char **tail_out)
{
    if (!body || !prefix || !id_out || !tail_out || !prefix_ci(body, prefix)) return 0;
    const char *p = body + strlen(prefix);
    if (*p && !isspace((unsigned char)*p)) return 0;
    while (isspace((unsigned char)*p)) ++p;
    errno = 0;
    char *end = NULL;
    long long id = strtoll(p, &end, 10);
    if (errno == ERANGE || end == p || id <= 0) return -1;
    while (isspace((unsigned char)*end)) ++end;
    if (*end != ':' && *end != '|') return -1;
    *id_out = (int64_t)id;
    *tail_out = end + 1;
    return 1;
}

static int contains_ci(const char *text, const char *part)
{
    size_t n = strlen(part);
    for (const char *p = text; p && *p; ++p) {
        size_t i = 0;
        while (i < n && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)part[i])) ++i;
        if (i == n) return 1;
    }
    return 0;
}

static int natural_due_date(const char *text, char out[11])
{
    for (const char *p = text; p && *p; ++p) {
        if (isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1]) &&
            isdigit((unsigned char)p[2]) && isdigit((unsigned char)p[3]) &&
            p[4] == '-' && isdigit((unsigned char)p[5]) &&
            isdigit((unsigned char)p[6]) && p[7] == '-' &&
            isdigit((unsigned char)p[8]) && isdigit((unsigned char)p[9])) {
            memcpy(out, p, 10); out[10] = '\0';
            if (validation_is_valid_date(out)) return 1;
        }
    }
    time_t now = time(NULL); struct tm date;
    if (!localtime_r(&now, &date)) return 0;
    int offset = -1;
    if (contains_ci(text, "hoje")) offset = 0;
    else if (contains_ci(text, "amanha") || contains_ci(text, "amanhã")) offset = 1;
    else {
        static const char *days[] = { "domingo", "segunda", "terca", "terça", "quarta", "quinta", "sexta", "sabado", "sábado" };
        int target = -1;
        for (int i = 0; i < 9 && target < 0; ++i) if (contains_ci(text, days[i])) target = i % 7;
        if (target >= 0) { offset = (target - date.tm_wday + 7) % 7; if (!offset) offset = 7; }
    }
    if (offset < 0) return 0;
    date.tm_mday += offset; date.tm_hour = 12; date.tm_min = 0; date.tm_sec = 0;
    if (mktime(&date) == (time_t)-1) return 0;
    return strftime(out, 11, "%Y-%m-%d", &date) == 10;
}

static int natural_teacher_task(const char *body, PendingTask *task, char *class_name, size_t class_cap)
{
#if defined(AI_USE_REMOTE)
    if (ai_remote_enabled()) {
        TaskData extracted;
        int rc = ai_parse_teacher_message(body, &extracted);
        if (rc != SUCCESS) return -1;
        snprintf(class_name, class_cap, "%s", extracted.turma);
        snprintf(task->subject, sizeof task->subject, "%s", extracted.disciplina);
        snprintf(task->title, sizeof task->title, "%.120s", extracted.descricao);
        snprintf(task->description, sizeof task->description, "%s", extracted.descricao);
        memcpy(task->due_date, extracted.data_entrega, 10);
        task->due_date[10] = '\0';
        return 1;
    }
#endif
    Class mentioned;
    if (class_mentioned(body, &mentioned))
        snprintf(class_name, class_cap, "%s", mentioned.name);
    else {
        const char *p = body;
        while (*p && isspace((unsigned char)*p)) ++p;
        if (prefix_ci(p, "turma")) { p += 5; while (*p && (isspace((unsigned char)*p) || *p == ':')) ++p; }
        size_t used = 0;
        while (p[used] && !isspace((unsigned char)p[used]) && used + 1 < class_cap) { class_name[used] = p[used]; ++used; }
        class_name[used] = '\0';
    }
    if (!class_name[0]) return 0;
    if (contains_ci(body, "matem")) snprintf(task->subject, sizeof task->subject, "Matemática");
    else if (contains_ci(body, "portugu")) snprintf(task->subject, sizeof task->subject, "Português");
    else if (contains_ci(body, "hist")) snprintf(task->subject, sizeof task->subject, "História");
    else if (contains_ci(body, "ciên") || contains_ci(body, "cien")) snprintf(task->subject, sizeof task->subject, "Ciências");
    else if (contains_ci(body, "ingl")) snprintf(task->subject, sizeof task->subject, "Inglês");
    else if (contains_ci(body, "geograf")) snprintf(task->subject, sizeof task->subject, "Geografia");
    else if (contains_ci(body, "fisic")) snprintf(task->subject, sizeof task->subject, "Física");
    else if (contains_ci(body, "quim")) snprintf(task->subject, sizeof task->subject, "Química");
    else return 0;
    if (!natural_due_date(body, task->due_date)) return 0;
    snprintf(task->description, sizeof task->description, "%s", body);
    snprintf(task->title, sizeof task->title, "%.120s", body);
    return 1;
}

static void trim(char *text)
{
    char *start = text;
    while (isspace((unsigned char)*start)) ++start;
    if (start != text) memmove(text, start, strlen(start) + 1);
    size_t len = strlen(text);
    while (len && isspace((unsigned char)text[len - 1])) text[--len] = '\0';
}

/* Campo em: "tarefa | turma: 9A | materia: Matematica | ..." */
static int field(const char *body, const char *name, char *out, size_t cap)
{
    const char *part = body;
    while (part && *part) {
        const char *end = strpbrk(part, "|\n");
        size_t len = end ? (size_t)(end - part) : strlen(part);
        if (len < 1200) {
            char item[1200];
            memcpy(item, part, len); item[len] = '\0'; trim(item);
            char *colon = strchr(item, ':');
            if (colon) {
                *colon = '\0'; trim(item); trim(colon + 1);
                if (equal_ci(item, name) && colon[1] && strlen(colon + 1) < cap) {
                    snprintf(out, cap, "%s", colon + 1);
                    return 1;
                }
            }
        }
        part = end ? end + 1 : NULL;
    }
    return 0;
}

static int teacher_id(int64_t user_id, int64_t *out)
{
    sqlite3_stmt *st = NULL;
    if (db_prepare("SELECT id FROM teachers WHERE user_id=?;", &st) != SUCCESS) return ERROR_DATABASE;
    db_bind_int64(st, 1, (long long)user_id);
    int found = sqlite3_step(st) == SQLITE_ROW;
    if (found) *out = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return found ? SUCCESS : ERROR_NOT_FOUND;
}

static int pending_save(const char *phone, const PendingTask *task)
{
    sqlite3_stmt *st = NULL;
    int rc = db_prepare("INSERT INTO pending_tasks(phone,teacher_id,class_id,subject,title,description,due_date) "
                        "VALUES(?,?,?,?,?,?,?) ON CONFLICT(phone) DO NOTHING;", &st);
    if (rc != SUCCESS) return rc;
    db_bind_text(st,1,phone); db_bind_int64(st,2,task->teacher_id); db_bind_int64(st,3,task->class_id);
    db_bind_text(st,4,task->subject); db_bind_text(st,5,task->title); db_bind_text(st,6,task->description); db_bind_text(st,7,task->due_date);
    int result = sqlite3_step(st);
    int inserted = result == SQLITE_DONE && sqlite3_changes(database_get()) == 1;
    sqlite3_finalize(st);
    return result != SQLITE_DONE ? ERROR_DATABASE : inserted ? SUCCESS : ERROR_ALREADY_EXISTS;
}

static int pending_load(const char *phone, PendingTask *task)
{
    sqlite3_stmt *st = NULL;
    if (db_prepare("SELECT teacher_id,class_id,subject,title,description,due_date FROM pending_tasks WHERE phone=?;", &st) != SUCCESS) return ERROR_DATABASE;
    db_bind_text(st,1,phone);
    if (sqlite3_step(st) != SQLITE_ROW) { sqlite3_finalize(st); return ERROR_NOT_FOUND; }
    task->teacher_id=sqlite3_column_int64(st,0); task->class_id=sqlite3_column_int64(st,1);
    snprintf(task->subject,sizeof task->subject,"%s",(const char *)sqlite3_column_text(st,2));
    snprintf(task->title,sizeof task->title,"%s",(const char *)sqlite3_column_text(st,3));
    snprintf(task->description,sizeof task->description,"%s",(const char *)sqlite3_column_text(st,4));
    snprintf(task->due_date,sizeof task->due_date,"%s",(const char *)sqlite3_column_text(st,5));
    sqlite3_finalize(st); return SUCCESS;
}

static void pending_delete(const char *phone)
{
    sqlite3_stmt *st = NULL;
    if (db_prepare("DELETE FROM pending_tasks WHERE phone=?;", &st) == SUCCESS) {
        db_bind_text(st,1,phone); (void)sqlite3_step(st); sqlite3_finalize(st);
    }
}

static int pending_task_edit_save(const char *from_ref, int64_t user_id,
                                  int64_t task_id, const PendingTask *task)
{
    sqlite3_stmt *st = NULL;
    if (db_prepare("INSERT INTO pending_teacher_task_edits(from_ref,user_id,task_id,teacher_id,class_id,subject,title,description,due_date) "
                   "VALUES(?,?,?,?,?,?,?,?,?) ON CONFLICT(from_ref) DO NOTHING;", &st) != SUCCESS)
        return ERROR_DATABASE;
    db_bind_text(st, 1, from_ref); db_bind_int64(st, 2, (long long)user_id);
    db_bind_int64(st, 3, (long long)task_id); db_bind_int64(st, 4, (long long)task->teacher_id);
    db_bind_int64(st, 5, (long long)task->class_id); db_bind_text(st, 6, task->subject);
    db_bind_text(st, 7, task->title); db_bind_text(st, 8, task->description);
    db_bind_text(st, 9, task->due_date);
    int rc = sqlite3_step(st);
    int inserted = rc == SQLITE_DONE && sqlite3_changes(database_get()) == 1;
    sqlite3_finalize(st);
    return rc != SQLITE_DONE ? ERROR_DATABASE : inserted ? SUCCESS : ERROR_ALREADY_EXISTS;
}

static int pending_task_edit_load(const char *from_ref, PendingTaskEdit *edit)
{
    sqlite3_stmt *st = NULL;
    if (db_prepare("SELECT user_id,task_id,teacher_id,class_id,subject,title,description,due_date "
                   "FROM pending_teacher_task_edits WHERE from_ref=?;", &st) != SUCCESS)
        return ERROR_DATABASE;
    db_bind_text(st, 1, from_ref);
    if (sqlite3_step(st) != SQLITE_ROW) { sqlite3_finalize(st); return ERROR_NOT_FOUND; }
    edit->user_id = sqlite3_column_int64(st, 0);
    edit->task_id = sqlite3_column_int64(st, 1);
    edit->task.teacher_id = sqlite3_column_int64(st, 2);
    edit->task.class_id = sqlite3_column_int64(st, 3);
    snprintf(edit->task.subject, sizeof edit->task.subject, "%s", (const char *)sqlite3_column_text(st, 4));
    snprintf(edit->task.title, sizeof edit->task.title, "%s", (const char *)sqlite3_column_text(st, 5));
    snprintf(edit->task.description, sizeof edit->task.description, "%s", (const char *)sqlite3_column_text(st, 6));
    snprintf(edit->task.due_date, sizeof edit->task.due_date, "%s", (const char *)sqlite3_column_text(st, 7));
    sqlite3_finalize(st);
    return SUCCESS;
}

static void pending_task_edit_delete(const char *from_ref, int64_t user_id)
{
    sqlite3_stmt *st = NULL;
    if (db_prepare("DELETE FROM pending_teacher_task_edits WHERE from_ref=? AND user_id=?;", &st) == SUCCESS) {
        db_bind_text(st, 1, from_ref); db_bind_int64(st, 2, (long long)user_id);
        (void)sqlite3_step(st); sqlite3_finalize(st);
    }
}

static int pending_notice_edit_save(const char *from_ref, int64_t user_id,
                                    int64_t notice_id, int64_t class_id,
                                    const char *title, const char *content)
{
    sqlite3_stmt *st = NULL;
    if (db_prepare("INSERT INTO pending_teacher_notice_edits(from_ref,user_id,notice_id,class_id,title,content) "
                   "VALUES(?,?,?,?,?,?) ON CONFLICT(from_ref) DO NOTHING;", &st) != SUCCESS)
        return ERROR_DATABASE;
    db_bind_text(st, 1, from_ref); db_bind_int64(st, 2, (long long)user_id);
    db_bind_int64(st, 3, (long long)notice_id); db_bind_int64(st, 4, (long long)class_id);
    db_bind_text(st, 5, title); db_bind_text(st, 6, content);
    int rc = sqlite3_step(st);
    int inserted = rc == SQLITE_DONE && sqlite3_changes(database_get()) == 1;
    sqlite3_finalize(st);
    return rc != SQLITE_DONE ? ERROR_DATABASE : inserted ? SUCCESS : ERROR_ALREADY_EXISTS;
}

static int pending_notice_edit_load(const char *from_ref, PendingNoticeEdit *edit)
{
    sqlite3_stmt *st = NULL;
    if (db_prepare("SELECT user_id,notice_id,class_id,title,content FROM pending_teacher_notice_edits WHERE from_ref=?;", &st) != SUCCESS)
        return ERROR_DATABASE;
    db_bind_text(st, 1, from_ref);
    if (sqlite3_step(st) != SQLITE_ROW) { sqlite3_finalize(st); return ERROR_NOT_FOUND; }
    edit->user_id = sqlite3_column_int64(st, 0);
    edit->notice_id = sqlite3_column_int64(st, 1);
    edit->class_id = sqlite3_column_int64(st, 2);
    snprintf(edit->title, sizeof edit->title, "%s", (const char *)sqlite3_column_text(st, 3));
    snprintf(edit->content, sizeof edit->content, "%s", (const char *)sqlite3_column_text(st, 4));
    sqlite3_finalize(st);
    return SUCCESS;
}

static void pending_notice_edit_delete(const char *from_ref, int64_t user_id)
{
    sqlite3_stmt *st = NULL;
    if (db_prepare("DELETE FROM pending_teacher_notice_edits WHERE from_ref=? AND user_id=?;", &st) == SUCCESS) {
        db_bind_text(st, 1, from_ref); db_bind_int64(st, 2, (long long)user_id);
        (void)sqlite3_step(st); sqlite3_finalize(st);
    }
}

static int teacher_pending_count(const char *from_ref)
{
    static const char sql[] =
        "SELECT (SELECT count(*) FROM pending_tasks WHERE phone=?) +"
        " (SELECT count(*) FROM pending_teacher_task_edits WHERE from_ref=?) +"
        " (SELECT count(*) FROM pending_teacher_notices WHERE from_ref=?) +"
        " (SELECT count(*) FROM pending_teacher_notice_edits WHERE from_ref=?);";
    sqlite3_stmt *st = NULL;
    if (db_prepare(sql, &st) != SUCCESS) return -1;
    for (int i = 1; i <= 4; ++i) db_bind_text(st, i, from_ref);
    int count = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int(st, 0) : -1;
    sqlite3_finalize(st);
    return count;
}

static void teacher_pending_clear(const char *from_ref, int64_t user_id)
{
    pending_delete(from_ref);
    pending_task_edit_delete(from_ref, user_id);
    sqlite3_stmt *st = NULL;
    if (db_prepare("DELETE FROM pending_teacher_notices WHERE from_ref=? AND user_id=?;", &st) == SUCCESS) {
        db_bind_text(st, 1, from_ref); db_bind_int64(st, 2, (long long)user_id);
        (void)sqlite3_step(st); sqlite3_finalize(st);
    }
    pending_notice_edit_delete(from_ref, user_id);
}

static int teacher_task(const char *phone, int64_t user_id, const char *body, char *reply, size_t cap)
{
    int pending_count = teacher_pending_count(phone);
    if (pending_count < 0) {
        snprintf(reply, cap, "Não consegui verificar suas publicações pendentes.");
        return SUCCESS;
    }
    if (pending_count > 0) {
        snprintf(reply, cap, "Voce ja tem uma publicacao aguardando confirmacao. Responda SIM ou CANCELAR antes de iniciar outra.");
        return SUCCESS;
    }
    PendingTask existing;
    if (pending_load(phone, &existing) == SUCCESS) {
        snprintf(reply, cap, "Voce ja tem uma tarefa aguardando confirmacao. Responda SIM para publicar, EDITAR para substituir ou CANCELAR para descartar.");
        return SUCCESS;
    }
    PendingTask task = {0}; char class_name[64] = {0}; Class classroom;
    int structured = field(body,"turma",class_name,sizeof class_name) &&
        field(body,"materia",task.subject,sizeof task.subject) &&
        field(body,"titulo",task.title,sizeof task.title) &&
        field(body,"entrega",task.due_date,sizeof task.due_date);
    if (structured) (void)field(body,"descricao",task.description,sizeof task.description);
    else {
        int parsed = natural_teacher_task(body, &task, class_name, sizeof class_name);
        if (parsed < 0) {
            snprintf(reply,cap,"Nao consegui interpretar a tarefa agora. Confira os dados ou tente novamente mais tarde."); return SUCCESS;
        }
        if (!parsed) {
        snprintf(reply,cap,"Faltam dados. Envie turma, materia, conteudo e data de entrega. Ex.: 9A matematica pagina 32 entrega sexta."); return SUCCESS;
        }
    }
    if (!validation_is_valid_date(task.due_date)) { snprintf(reply,cap,"A entrega deve usar AAAA-MM-DD."); return SUCCESS; }
    if (teacher_id(user_id,&task.teacher_id) != SUCCESS) {
        snprintf(reply,cap,"Seu cadastro de professor nao foi encontrado. Procure a coordenacao."); return SUCCESS;
    }
    char opcoes[256] = {0};
    int achou = resolve_class_reference(class_name, &classroom, opcoes, sizeof opcoes);
    if (achou == 2) {
        snprintf(reply, cap,
            "Mais de uma turma combina com %s: %s. Envie de novo com o nome certo.",
            class_name, opcoes);
        return SUCCESS;
    }
    if (achou != 1) {
        snprintf(reply, cap,
            "A turma %s nao esta cadastrada. Peça a coordenacao para cria-la antes de enviar a tarefa.",
            class_name);
        return SUCCESS;
    }
    if (strcmp(classroom.status, "ACTIVE") != 0) {
        snprintf(reply,cap,"A turma %s nao esta ativa.",class_name); return SUCCESS;
    }
    task.class_id=classroom.id;
    if (!classes_teacher_has_access(task.teacher_id,task.class_id)) {
        snprintf(reply, cap,
            "Voce nao esta autorizado a cadastrar tarefas para a turma %s. Peça a coordenacao para liberar o acesso.",
            classroom.name);
        return SUCCESS;
    }
    int pending_rc = pending_save(phone,&task);
    if (pending_rc != SUCCESS) {
        snprintf(reply,cap,pending_rc == ERROR_ALREADY_EXISTS
            ? "Ja existe uma tarefa aguardando confirmacao. Responda SIM, EDITAR ou CANCELAR."
            : "Nao consegui preparar a tarefa.");
        return SUCCESS;
    }
    snprintf(reply,cap,"Resumo: %s, turma %s, %s, entrega %s. Esta correto? Responda SIM ou EDITAR. Para descartar, CANCELAR.",task.subject,classroom.name,task.title,task.due_date);
    return SUCCESS;
}

static void teacher_list_tasks(int64_t user_id, char *reply, size_t cap)
{
    int64_t id = 0;
    if (teacher_id(user_id, &id) != SUCCESS) {
        snprintf(reply, cap, "Seu cadastro de professor não foi encontrado.");
        return;
    }
    Task *items = NULL;
    size_t count = 0;
    if (tasks_list_by_teacher(id, false, &items, &count) != SUCCESS) {
        snprintf(reply, cap, "Não consegui consultar suas tarefas agora.");
        return;
    }
    if (count == 0) {
        snprintf(reply, cap, "Você não tem tarefas ativas. Envie uma tarefa com turma, matéria, conteúdo e entrega para cadastrar.");
        free(items);
        return;
    }
    size_t used = (size_t)snprintf(reply, cap, "Suas próximas tarefas:\n");
    if (used >= cap) used = cap - 1;
    size_t shown = count < 8 ? count : 8;
    for (size_t i = 0; i < shown && used < cap; ++i) {
        Class classroom;
        const char *class_name = classes_find_by_id(items[i].class_id, &classroom) == SUCCESS
            ? classroom.name : "Turma indisponível";
        int n = snprintf(reply + used, cap - used,
                         "#%lld — %s (%s), entrega %s\n",
                         (long long)items[i].id, items[i].title,
                         class_name, items[i].due_date);
        if (n < 0 || (size_t)n >= cap - used) { used = cap - 1; break; }
        used += (size_t)n;
    }
    if (count > shown && used < cap)
        (void)snprintf(reply + used, cap - used, "Para editar: EDITAR TAREFA <id> | matéria: ... | título: ... | entrega: AAAA-MM-DD");
    else if (used < cap)
        (void)snprintf(reply + used, cap - used, "Para editar: EDITAR TAREFA <id> | matéria: ... | título: ... | entrega: AAAA-MM-DD");
    free(items);
}

static void teacher_list_reminders(int64_t user_id, char *reply, size_t cap)
{
    sqlite3_stmt *st = NULL;
    const char *sql =
        "SELECT n.id,c.name,n.title,n.content FROM notices n "
        "JOIN classes c ON c.id=n.class_id "
        "WHERE n.author_id=? AND n.status='ACTIVE' AND EXISTS ("
        " SELECT 1 FROM teachers t JOIN teacher_classes tc ON tc.teacher_id=t.id "
        " WHERE t.user_id=? AND tc.class_id=n.class_id) "
        "ORDER BY n.id DESC LIMIT 8;";
    if (db_prepare(sql, &st) != SUCCESS) {
        snprintf(reply, cap, "Não consegui consultar seus lembretes agora.");
        return;
    }
    db_bind_int64(st, 1, (long long)user_id);
    db_bind_int64(st, 2, (long long)user_id);
    size_t used = 0, count = 0;
    int step;
    while ((step = sqlite3_step(st)) == SQLITE_ROW && used < cap) {
        int64_t id = sqlite3_column_int64(st, 0);
        const char *class_name = (const char *)sqlite3_column_text(st, 1);
        const char *title = (const char *)sqlite3_column_text(st, 2);
        const char *content = (const char *)sqlite3_column_text(st, 3);
        char short_content[121];
        snprintf(short_content, sizeof short_content, "%.117s%s",
                 content ? content : "", content && strlen(content) > 117 ? "..." : "");
        int n = snprintf(reply + used, cap - used, "%s#%lld — %s (%s): %s",
                         count ? "\n" : "Seus lembretes ativos:\n",
                         (long long)id, title ? title : "Lembrete",
                         class_name ? class_name : "Turma", short_content);
        if (n < 0 || (size_t)n >= cap - used) { used = cap - 1; break; }
        used += (size_t)n;
        ++count;
    }
    sqlite3_finalize(st);
    if (step != SQLITE_DONE && used == 0) {
        snprintf(reply, cap, "Não consegui consultar seus lembretes agora.");
    } else if (count == 0) {
        snprintf(reply, cap, "Você ainda não publicou lembretes para suas turmas.");
    } else if (used < cap) {
        (void)snprintf(reply + used, cap - used, "\nPara editar: EDITAR LEMBRETE <id>: novo texto");
    }
}

static int teacher_prepare_task_edit(const char *from_ref, int64_t user_id,
                                     int64_t task_id, const char *fields,
                                     char *reply, size_t cap)
{
    if (teacher_pending_count(from_ref) != 0) {
        snprintf(reply, cap, "Resolva a publicação pendente com SIM ou CANCELAR antes de editar outra.");
        return SUCCESS;
    }
    int64_t current_teacher_id = 0;
    Task current;
    Class classroom;
    if (teacher_id(user_id, &current_teacher_id) != SUCCESS ||
        tasks_find_by_id(task_id, &current) != SUCCESS) {
        snprintf(reply, cap, "Não encontrei essa tarefa ativa no seu cadastro.");
        return SUCCESS;
    }
    if (current.teacher_id != current_teacher_id) {
        snprintf(reply, cap, "Você só pode editar tarefas que cadastrou.");
        return SUCCESS;
    }
    if (classes_find_by_id(current.class_id, &classroom) != SUCCESS ||
        strcmp(classroom.status, "ACTIVE") != 0 ||
        !classes_teacher_has_access(current_teacher_id, current.class_id)) {
        snprintf(reply, cap, "Você não está mais autorizado a editar essa tarefa para a turma.");
        return SUCCESS;
    }

    PendingTask updated = {0};
    updated.teacher_id = current.teacher_id;
    updated.class_id = current.class_id;
    snprintf(updated.subject, sizeof updated.subject, "%s", current.subject);
    snprintf(updated.title, sizeof updated.title, "%s", current.title);
    snprintf(updated.description, sizeof updated.description, "%s", current.description);
    snprintf(updated.due_date, sizeof updated.due_date, "%s", current.due_date);
    int changed = 0;
    changed |= field(fields, "materia", updated.subject, sizeof updated.subject);
    changed |= field(fields, "matéria", updated.subject, sizeof updated.subject);
    changed |= field(fields, "titulo", updated.title, sizeof updated.title);
    changed |= field(fields, "título", updated.title, sizeof updated.title);
    changed |= field(fields, "descricao", updated.description, sizeof updated.description);
    changed |= field(fields, "descrição", updated.description, sizeof updated.description);
    changed |= field(fields, "entrega", updated.due_date, sizeof updated.due_date);
    if (!changed || !validation_is_valid_date(updated.due_date)) {
        snprintf(reply, cap, "Informe ao menos um campo para alterar. Use matéria, título, descrição ou entrega (AAAA-MM-DD).");
        return SUCCESS;
    }
    int rc = pending_task_edit_save(from_ref, user_id, task_id, &updated);
    if (rc != SUCCESS) {
        snprintf(reply, cap, "Já existe uma alteração pendente para esse WhatsApp. Responda SIM ou CANCELAR.");
        return SUCCESS;
    }
    snprintf(reply, cap,
             "Prévia da tarefa #%lld — %s, turma %s, entrega %s. Responda SIM para salvar ou CANCELAR para descartar.",
             (long long)task_id, updated.title, classroom.name, updated.due_date);
    return SUCCESS;
}

static int teacher_prepare_notice_edit(const char *from_ref, int64_t user_id,
                                       int64_t notice_id, const char *content,
                                       char *reply, size_t cap)
{
    if (teacher_pending_count(from_ref) != 0) {
        snprintf(reply, cap, "Resolva a publicação pendente com SIM ou CANCELAR antes de editar outra.");
        return SUCCESS;
    }
    if (!validation_length_between(content, 3, 2000)) {
        snprintf(reply, cap, "O novo texto do lembrete deve ter entre 3 e 2000 caracteres.");
        return SUCCESS;
    }
    int64_t current_teacher_id = 0;
    if (teacher_id(user_id, &current_teacher_id) != SUCCESS) {
        snprintf(reply, cap, "Seu cadastro de professor não foi encontrado.");
        return SUCCESS;
    }
    sqlite3_stmt *st = NULL;
    const char *sql =
        "SELECT n.class_id,n.title FROM notices n JOIN classes c ON c.id=n.class_id "
        "WHERE n.id=? AND n.author_id=? AND n.status='ACTIVE' AND c.status='ACTIVE' "
        "AND EXISTS(SELECT 1 FROM teacher_classes tc WHERE tc.teacher_id=? AND tc.class_id=n.class_id);";
    if (db_prepare(sql, &st) != SUCCESS) {
        snprintf(reply, cap, "Não consegui preparar a edição do lembrete.");
        return SUCCESS;
    }
    db_bind_int64(st, 1, (long long)notice_id);
    db_bind_int64(st, 2, (long long)user_id);
    db_bind_int64(st, 3, (long long)current_teacher_id);
    if (sqlite3_step(st) != SQLITE_ROW) {
        sqlite3_finalize(st);
        snprintf(reply, cap, "Lembrete não encontrado ou você não tem acesso para editá-lo.");
        return SUCCESS;
    }
    int64_t class_id = sqlite3_column_int64(st, 0);
    char title[121];
    snprintf(title, sizeof title, "%s", (const char *)sqlite3_column_text(st, 1));
    sqlite3_finalize(st);
    int rc = pending_notice_edit_save(from_ref, user_id, notice_id, class_id, title, content);
    if (rc != SUCCESS) {
        snprintf(reply, cap, "Já existe uma alteração pendente para esse WhatsApp. Responda SIM ou CANCELAR.");
        return SUCCESS;
    }
    Class classroom;
    const char *class_name = classes_find_by_id(class_id, &classroom) == SUCCESS
        ? classroom.name : "sua turma";
    snprintf(reply, cap, "Prévia do lembrete #%lld para %s: %.700s. Responda SIM para salvar ou CANCELAR para descartar.",
             (long long)notice_id, class_name, content);
    return SUCCESS;
}

static void teacher_confirm_task_edit(const char *from_ref, int64_t user_id,
                                      const PendingTaskEdit *edit,
                                      char *reply, size_t cap)
{
    int64_t actual_teacher_id = 0;
    Task current;
    Class classroom;
    if (edit->user_id != user_id || teacher_id(user_id, &actual_teacher_id) != SUCCESS ||
        actual_teacher_id != edit->task.teacher_id ||
        tasks_find_by_id(edit->task_id, &current) != SUCCESS ||
        current.teacher_id != actual_teacher_id || current.class_id != edit->task.class_id ||
        classes_find_by_id(current.class_id, &classroom) != SUCCESS ||
        strcmp(classroom.status, "ACTIVE") != 0 ||
        !classes_teacher_has_access(actual_teacher_id, current.class_id)) {
        pending_task_edit_delete(from_ref, user_id);
        snprintf(reply, cap, "A tarefa não está mais disponível para edição por este professor.");
        return;
    }
    int rc = tasks_update(edit->task_id, actual_teacher_id, false,
                          edit->task.subject, edit->task.title,
                          edit->task.description, edit->task.due_date);
    if (rc != SUCCESS) {
        snprintf(reply, cap, "Não consegui atualizar a tarefa. A alteração continua pendente; responda SIM para tentar novamente ou CANCELAR.");
        return;
    }
    pending_task_edit_delete(from_ref, user_id);
    snprintf(reply, cap, "Tarefa #%lld atualizada.", (long long)edit->task_id);
}

static void teacher_confirm_notice_edit(const char *from_ref, int64_t user_id,
                                        const PendingNoticeEdit *edit,
                                        char *reply, size_t cap)
{
    int64_t actual_teacher_id = 0;
    if (edit->user_id != user_id || teacher_id(user_id, &actual_teacher_id) != SUCCESS) {
        pending_notice_edit_delete(from_ref, user_id);
        snprintf(reply, cap, "Não foi possível confirmar o lembrete para esse cadastro.");
        return;
    }
    if (db_begin() != SUCCESS) {
        snprintf(reply, cap, "Não consegui iniciar a atualização do lembrete.");
        return;
    }
    sqlite3_stmt *st = NULL;
    const char *update_sql =
        "UPDATE notices SET title=?,content=? WHERE id=? AND author_id=? AND class_id=? "
        "AND status='ACTIVE' AND EXISTS(SELECT 1 FROM classes c WHERE c.id=notices.class_id "
        "AND c.status='ACTIVE') AND EXISTS(SELECT 1 FROM teacher_classes tc "
        "WHERE tc.teacher_id=? AND tc.class_id=notices.class_id);";
    int ok = db_prepare(update_sql, &st) == SUCCESS;
    if (ok) {
        db_bind_text(st, 1, edit->title); db_bind_text(st, 2, edit->content);
        db_bind_int64(st, 3, (long long)edit->notice_id);
        db_bind_int64(st, 4, (long long)user_id);
        db_bind_int64(st, 5, (long long)edit->class_id);
        db_bind_int64(st, 6, (long long)actual_teacher_id);
        ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(database_get()) == 1;
        sqlite3_finalize(st);
    }
    if (ok && db_prepare("DELETE FROM pending_teacher_notice_edits WHERE from_ref=? AND user_id=? AND notice_id=?;", &st) == SUCCESS) {
        db_bind_text(st, 1, from_ref); db_bind_int64(st, 2, (long long)user_id);
        db_bind_int64(st, 3, (long long)edit->notice_id);
        ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(database_get()) == 1;
        sqlite3_finalize(st);
    } else ok = 0;
    if (!ok || db_commit() != SUCCESS) {
        (void)db_rollback();
        snprintf(reply, cap, "O lembrete deixou de estar autorizado ou não pôde ser atualizado.");
        return;
    }
    snprintf(reply, cap, "Lembrete #%lld atualizado.", (long long)edit->notice_id);
}

static int class_mentioned(const char *body, Class *classroom)
{
    Class *classes = NULL; size_t count = 0;
    if (classes_list(&classes, &count) != SUCCESS) return 0;
    int found = 0;
    for (size_t i = 0; i < count && !found; ++i) {
        size_t n = strlen(classes[i].name);
        for (const char *p = body; *p; ++p) {
            size_t j = 0;
            while (j < n && p[j] && tolower((unsigned char)p[j]) == tolower((unsigned char)classes[i].name[j])) ++j;
            if (j == n && (p == body || !isalnum((unsigned char)p[-1])) &&
                (!p[j] || !isalnum((unsigned char)p[j]))) {
                *classroom = classes[i]; found = 1; break;
            }
        }
    }
    free(classes);
    return found;
}

static int student_tasks_for_class(int64_t class_id, const Class *classroom,
                                   const char *query, char *reply, size_t cap)
{
    Task *list = NULL; size_t count = 0;
    if (tasks_list_by_class(class_id, false, &list, &count) != SUCCESS) {
        snprintf(reply, cap, "Nao consegui consultar as tarefas agora."); return SUCCESS;
    }
    time_t now = time(NULL); struct tm today_tm;
    if (!localtime_r(&now, &today_tm)) { free(list); snprintf(reply, cap, "Nao consegui consultar as datas agora."); return SUCCESS; }
    today_tm.tm_hour = 12; today_tm.tm_min = 0; today_tm.tm_sec = 0;
    char today[11] = {0}, tomorrow[11] = {0}, week_end[11] = {0};
    (void)strftime(today, sizeof today, "%Y-%m-%d", &today_tm);
    today_tm.tm_mday += 1; (void)mktime(&today_tm);
    (void)strftime(tomorrow, sizeof tomorrow, "%Y-%m-%d", &today_tm);
    today_tm.tm_mday += 6; (void)mktime(&today_tm);
    (void)strftime(week_end, sizeof week_end, "%Y-%m-%d", &today_tm);
    int exact_tomorrow = contains_ci(query, "amanha") || contains_ci(query, "amanhã");
    int today_only = contains_ci(query, "hoje");
    int week = contains_ci(query, "semana");
    int heading = snprintf(reply, cap, "Tarefas de %s:\n", classroom->name);
    if (heading < 0 || (size_t)heading >= cap) {
        if (cap) reply[cap - 1] = '\0';
        free(list);
        return SUCCESS;
    }
    size_t used = (size_t)heading;
    int shown = 0;
    if (!count) {
        int n = snprintf(reply + used, cap - used, "Nenhuma tarefa ativa.");
        if (n < 0 || (size_t)n >= cap - used) reply[cap - 1] = '\0';
        free(list);
        return SUCCESS;
    }
    for (size_t i = 0; i < count && used + 1 < cap; ++i) {
        const char *due = list[i].due_date;
        if ((exact_tomorrow && strcmp(due, tomorrow) != 0) ||
            (today_only && strcmp(due, today) != 0) ||
            (week && (strcmp(due, today) < 0 || strcmp(due, week_end) > 0)) ||
            (!exact_tomorrow && !today_only && !week && strcmp(due, today) < 0)) continue;
        if (shown >= 8) break;
        int n = snprintf(reply + used, cap - used, "- %s: %s (entrega %s)\n",
            list[i].subject, list[i].description[0] ? list[i].description : list[i].title,
            due);
        if (n < 0 || (size_t)n >= cap - used) { used = cap - 1; break; }
        used += (size_t)n;
        ++shown;
    }
    if (count && shown == 0 && used + 48 < cap)
        (void)snprintf(reply + used, cap - used, "Nenhuma tarefa nesse período.");
    free(list); return SUCCESS;
}

static int student_message(const char *body, int64_t assigned_class_id,
                                     char *reply, size_t cap)
{
    if (prefix_ci(body, "tarefa de aluno") || prefix_ci(body, "minha tarefa") ||
        contains_ci(body, "minha resposta") || contains_ci(body, "criar tarefa") ||
        contains_ci(body, "entrega: ") ||
        (contains_ci(body, "entrega") &&
         (contains_ci(body, "matem") || contains_ci(body, "portugu") ||
          contains_ci(body, "hist") || contains_ci(body, "cien") ||
          contains_ci(body, "ingl")))) {
        snprintf(reply, cap, "Este numero nao esta autorizado como professor. Alunos nao podem cadastrar tarefas; consulte as atividades ou procure a coordenacao.");
        return SUCCESS;
    }
    Class classroom;
    int asks_tasks = student_requests_tasks(body);
    int asks_notices = student_requests_notices(body);
    if (asks_tasks || asks_notices) {
        Class requested_class;
        char options[256] = {0};
        int class_match = resolve_class_reference(body, &requested_class,
                                                  options, sizeof options);
        if (class_match == 2) {
            snprintf(reply, cap, "Encontrei mais de uma turma para essa série: %s. "
                     "Qual é a letra da sua turma?", options);
            return SUCCESS;
        }
        if (class_match < 0) return ERROR_DATABASE;
        if (class_match == 1) {
            if (assigned_class_id <= 0 ||
                (int64_t)requested_class.id != assigned_class_id) {
                snprintf(reply, cap, "Só posso consultar tarefas e avisos da turma vinculada ao seu cadastro.");
                return SUCCESS;
            }
            classroom = requested_class;
        } else if (looks_like_class_reference(body)) {
            snprintf(reply, cap, "Essa turma ainda não está cadastrada. "
                     "Peça à coordenação para cadastrá-la.");
            return SUCCESS;
        }
    }
    if (assigned_class_id <= 0 || classes_find_by_id(assigned_class_id, &classroom) != SUCCESS ||
        strcmp(classroom.status, "ACTIVE") != 0) {
        if (asks_tasks || asks_notices) {
            snprintf(reply, cap, "De qual turma voce e? (ex.: 9º A, 1º EM)");
            return SUCCESS;
        }
        assigned_class_id = 0;
    }
    const char *question = body;
    if (asks_tasks && assigned_class_id > 0)
        return student_tasks_for_class(assigned_class_id, &classroom, question, reply, cap);
    if (asks_notices && assigned_class_id > 0) {
        sqlite3_stmt *st = NULL;
        if (db_prepare("SELECT title,content FROM notices WHERE status='ACTIVE' "
                       "AND publish_date<=date('now','localtime') "
                       "AND (class_id IS NULL OR class_id=?) ORDER BY publish_date DESC,id DESC LIMIT 5;", &st) != SUCCESS) {
            snprintf(reply, cap, "Nao consegui consultar os comunicados agora."); return SUCCESS;
        }
        db_bind_int64(st, 1, (long long)assigned_class_id);
        size_t used = (size_t)snprintf(reply, cap, "Comunicados de %s:\n", classroom.name);
        int count = 0;
        while (used + 1 < cap) {
            int step = sqlite3_step(st);
            if (step != SQLITE_ROW) break;
            int n = snprintf(reply + used, cap - used, "- %s: %s\n",
                (const char *)sqlite3_column_text(st, 0), (const char *)sqlite3_column_text(st, 1));
            if (n < 0 || (size_t)n >= cap - used) { used = cap - 1; break; }
            used += (size_t)n;
            ++count;
        }
        sqlite3_finalize(st);
        if (!count) snprintf(reply + used, cap - used, "Nenhum comunicado ativo.");
        return SUCCESS;
    }
    char context[1800] = {0};
    whatsapp_build_school_context_for_class(assigned_class_id, context, sizeof context);
    int ai_rc = ai_answer_student_question(question, context, reply, cap);
    if (ai_rc != SUCCESS || !reply[0]) {
        /* Falha remota é transitória: não enviar fallback como se fosse
         * resposta válida nem deixar o worker marcar a mensagem como atendida. */
        reply[0] = '\0';
        return ai_rc == SUCCESS ? ERROR_AI : ai_rc;
    }
    static const char disclaimer[] = "\n\nResposta feita por IA — pode errar. Confira com seu professor.";
    size_t used = strlen(reply), suffix = sizeof disclaimer - 1;
    if (used + suffix < cap) memcpy(reply + used, disclaimer, suffix + 1);
    return SUCCESS;
}

/* Normaliza referências de turma sem depender da IA: "9º ano", "9° ano",
 * "nono ano" e "9 A" podem representar a mesma turma. */
static int class_word_is_ignored(const char *word)
{
    static const char *ignored[] = {
        "ano", "anos", "serie", "series", "turma", "do", "da", "de",
        "meu", "minha", "meus", "minhas", "sou", "aluno", "aluna",
        "estudo", "estou", "no", "na", "classe", "sala", "com",
        "tarefas", "tarefa", "atividades", "atividade", "exercicios", "exercicio", "deveres", "dever",
        "licao", "licoes", "trabalhos", "trabalho", "avisos", "aviso", "recados", "recado",
        "comunicados", "comunicado", "provas", "prova", "avaliacoes", "avaliacao", "simulados", "simulado",
        "agenda", "casa", "quais", "qual", "sao", "tem", "ha", "existe",
        "existem", "liste", "lista", "listar", "mostra", "mostre", "mostrar", "manda",
        "mande", "passa", "passe", "envia", "envie", "diz", "diga", "fala",
        "fale", "quero", "queria", "gostaria", "preciso", "ver", "saber", "consultar",
        "novamente", "novo", "todas", "todos", "tudo", "as", "os", "o",
        "para", "pra", "pro", "pros", "me", "nos", "mim", "nossa",
        "nosso", "hoje", "amanha", "semana", "proxima", "proximo", "por", "favor",
        "pfv", "pf", "oi", "ola", "bom", "boa", "tarde", "noite"
    };
    for (size_t i = 0; i < sizeof ignored / sizeof ignored[0]; ++i)
        if (strcmp(word, ignored[i]) == 0) return 1;
    return 0;
}

static const char *class_ordinal_digit(const char *word)
{
    static const struct { const char *word, *digit; } ordinals[] = {
        {"primeiro", "1"}, {"primeira", "1"}, {"primeiros", "1"}, {"primeiras", "1"},
        {"segundo", "2"}, {"segunda", "2"}, {"segundos", "2"}, {"segundas", "2"},
        {"terceiro", "3"}, {"terceira", "3"}, {"terceiros", "3"}, {"terceiras", "3"},
        {"quarto", "4"}, {"quarta", "4"}, {"quartos", "4"}, {"quartas", "4"},
        {"quinto", "5"}, {"quinta", "5"}, {"quintos", "5"}, {"quintas", "5"},
        {"sexto", "6"}, {"sexta", "6"}, {"sextos", "6"}, {"sextas", "6"},
        {"setimo", "7"}, {"setima", "7"}, {"setimos", "7"}, {"setimas", "7"},
        {"oitavo", "8"}, {"oitava", "8"}, {"oitavos", "8"}, {"oitavas", "8"},
        {"nono", "9"}, {"nona", "9"}, {"nonos", "9"}, {"nonas", "9"},
        {"decimo", "10"}, {"decima", "10"}, {"decimos", "10"}, {"decimas", "10"}
    };
    for (size_t i = 0; i < sizeof ordinals / sizeof ordinals[0]; ++i)
        if (strcmp(word, ordinals[i].word) == 0) return ordinals[i].digit;
    return NULL;
}

static int class_append(char *out, size_t cap, size_t *used, const char *text)
{
    size_t length = strlen(text);
    if (length >= cap - *used) return 0;
    memcpy(out + *used, text, length);
    *used += length;
    out[*used] = '\0';
    return 1;
}

static int class_flush_word(char *word, char *out, size_t cap, size_t *used)
{
    if (!word[0]) return 1;
    const char *replacement = class_ordinal_digit(word);
    if (replacement) {
        word[0] = '\0';
        return class_append(out, cap, used, replacement);
    }
    if (class_word_is_ignored(word)) {
        word[0] = '\0';
        return 1;
    }

    /* Também aceita formas digitadas sem espaço: "9oano" e "9ano". */
    size_t digits = 0;
    while (isdigit((unsigned char)word[digits])) ++digits;
    if (digits > 0 && word[digits]) {
        const char *tail = word + digits;
        if (strcmp(tail, "ano") == 0 || strcmp(tail, "anos") == 0 ||
            strcmp(tail, "o") == 0 || strcmp(tail, "oano") == 0 ||
            strcmp(tail, "oanos") == 0 || strcmp(tail, "aserie") == 0 ||
            strcmp(tail, "aseries") == 0)
            word[digits] = '\0';
    }
    int ok = class_append(out, cap, used, word);
    word[0] = '\0';
    return ok;
}

static char class_ascii_letter(unsigned char first, unsigned char second)
{
    if (first != 0xC3) return '\0';
    if (second >= 0x80 && second <= 0x85) return 'a';
    if (second == 0x87) return 'c';
    if (second >= 0x88 && second <= 0x8B) return 'e';
    if (second >= 0x8C && second <= 0x8F) return 'i';
    if (second == 0x91) return 'n';
    if (second >= 0x92 && second <= 0x96) return 'o';
    if (second >= 0x99 && second <= 0x9C) return 'u';
    if (second == 0x9D || second == 0xBF) return 'y';
    if (second >= 0xA0 && second <= 0xA5) return 'a';
    if (second == 0xA7) return 'c';
    if (second >= 0xA8 && second <= 0xAB) return 'e';
    if (second >= 0xAC && second <= 0xAF) return 'i';
    if (second == 0xB1) return 'n';
    if (second >= 0xB2 && second <= 0xB6) return 'o';
    if (second >= 0xB9 && second <= 0xBC) return 'u';
    if (second == 0xBD) return 'y';
    return '\0';
}

static int normalize_class_reference(const char *input, char *out, size_t cap)
{
    if (!input || !out || cap < 2) return 0;
    out[0] = '\0';
    size_t used = 0, word_used = 0;
    char word[64] = {0};
    const unsigned char *p = (const unsigned char *)input;
    while (*p) {
        unsigned char ch = *p++;
        char normalized = '\0';
        if (ch < 0x80) {
            if (isalnum(ch)) normalized = (char)tolower(ch);
        } else if (ch == 0xC2 && (*p == 0xB0 || *p == 0xBA || *p == 0xAA)) {
            /* Graus e indicadores ordinais separam o número da palavra seguinte. */
            ++p;
        } else if (ch == 0xC3 && *p) {
            normalized = class_ascii_letter(ch, *p++);
        } else if ((ch & 0xE0) == 0xC0 && *p) {
            ++p;
        } else if ((ch & 0xF0) == 0xE0 && p[0] && p[1]) {
            p += 2;
        } else if ((ch & 0xF8) == 0xF0 && p[0] && p[1] && p[2]) {
            p += 3;
        }

        if (normalized) {
            if (word_used + 1 >= sizeof word) return 0;
            word[word_used++] = normalized;
            word[word_used] = '\0';
        } else if (!class_flush_word(word, out, cap, &used)) {
            return 0;
        } else {
            word_used = 0;
        }
    }
    if (!class_flush_word(word, out, cap, &used)) return 0;
    if (used && isalpha((unsigned char)out[0]) && strncmp(out, "id", 2) != 0) {
        size_t skip = 0;
        while (skip < used && isalpha((unsigned char)out[skip])) ++skip;
        size_t digits = skip;
        while (digits < used && isdigit((unsigned char)out[digits])) ++digits;
        if (digits > skip && digits < used && isalpha((unsigned char)out[digits])) {
            memmove(out, out + skip, used - skip + 1);
            used -= skip;
        }
    }
    return used > 0;
}

static int normalize_keyword_text(const char *input, char *out, size_t cap)
{
    if (!input || !out || cap < 2) return 0;
    size_t used = 0;
    out[0] = '\0';
    const unsigned char *p = (const unsigned char *)input;
    while (*p) {
        unsigned char ch = *p++;
        char normalized = '\0';
        if (ch < 0x80) {
            if (isalnum(ch)) normalized = (char)tolower(ch);
        } else if (ch == 0xC2 && *p) {
            ++p;
        } else if (ch == 0xC3 && *p) {
            normalized = class_ascii_letter(ch, *p++);
        } else if ((ch & 0xE0) == 0xC0 && *p) {
            ++p;
        } else if ((ch & 0xF0) == 0xE0 && p[0] && p[1]) {
            p += 2;
        } else if ((ch & 0xF8) == 0xF0 && p[0] && p[1] && p[2]) {
            p += 3;
        }
        if (normalized) {
            if (used + 1 >= cap) return 0;
            out[used++] = normalized;
        } else if (used > 0 && out[used - 1] != ' ') {
            if (used + 1 >= cap) return 0;
            out[used++] = ' ';
        }
    }
    while (used && out[used - 1] == ' ') --used;
    out[used] = '\0';
    return used > 0;
}

static int keyword_token_matches(const char *token, size_t token_len,
                                 const char *term, size_t term_len)
{
    if (token_len == term_len && memcmp(token, term, term_len) == 0) return 1;
    if (token_len == term_len + 1 && token[token_len - 1] == 's' &&
        memcmp(token, term, term_len) == 0) return 1;
    return token_len == term_len + 2 && token[token_len - 2] == 'e' &&
        token[token_len - 1] == 's' && memcmp(token, term, term_len) == 0;
}

static int text_mentions_any(const char *text, const char *const *terms, size_t count)
{
    char folded_text[8192] = {0};
    if (!normalize_keyword_text(text, folded_text, sizeof folded_text)) return 0;
    for (size_t i = 0; i < count; ++i) {
        char folded_term[128] = {0};
        if (!normalize_keyword_text(terms[i], folded_term, sizeof folded_term)) continue;
        size_t term_len = strlen(folded_term);
        const char *p = folded_text;
        while (*p) {
            while (*p == ' ') ++p;
            const char *end = p;
            while (*end && *end != ' ') ++end;
            if (keyword_token_matches(p, (size_t)(end - p), folded_term, term_len)) return 1;
            p = end;
        }
    }
    return 0;
}

static int student_requests_tasks(const char *text)
{
    static const char *terms[] = {
        "tarefa", "atividade", "dever", "licao", "trabalho", "prova",
        "avaliacao", "entregar", "entrega", "pendente", "exercicio", "prazo"
    };
    return text_mentions_any(text, terms, sizeof terms / sizeof terms[0]);
}

static int student_requests_notices(const char *text)
{
    static const char *terms[] = {
        "aviso", "recado", "comunicado", "noticia", "novidade", "evento",
        "informacao", "calendario"
    };
    return text_mentions_any(text, terms, sizeof terms / sizeof terms[0]);
}

static int class_numeric_stem(const char *key, char *out, size_t cap)
{
    size_t length = 0;
    while (key[length] && isdigit((unsigned char)key[length])) ++length;
    if (length == 0 || length >= cap) return 0;
    memcpy(out, key, length);
    out[length] = '\0';
    return 1;
}

static int parse_class_id_key(const char *key, int64_t *id)
{
    const char *digits = key;
    if (strncmp(key, "turmaid", 7) == 0) digits = key + 7;
    else if (strncmp(key, "codigo", 6) == 0) digits = key + 6;
    else if (strncmp(key, "id ", 3) == 0) digits = key + 3;
    else if (strncmp(key, "id", 2) == 0) digits = key + 2;
    else return 0;
    if (!*digits) return 0;
    for (const char *p = digits; *p; ++p)
        if (!isdigit((unsigned char)*p)) return 0;
    errno = 0;
    char *end = NULL;
    long long value = strtoll(digits, &end, 10);
    if (errno == ERANGE || !end || *end || value <= 0) return 0;
    *id = (int64_t)value;
    return 1;
}

static int parse_numeric_class_id(const char *key, int64_t *id)
{
    if (!key || !*key) return 0;
    for (const char *p = key; *p; ++p)
        if (!isdigit((unsigned char)*p)) return 0;
    errno = 0;
    char *end = NULL;
    long long value = strtoll(key, &end, 10);
    if (errno == ERANGE || !end || *end || value <= 0) return 0;
    *id = (int64_t)value;
    return 1;
}

static int is_plain_numeric_reference(const char *text)
{
    if (!text) return 0;
    while (isspace((unsigned char)*text)) ++text;
    if (!isdigit((unsigned char)*text)) return 0;
    while (isdigit((unsigned char)*text)) ++text;
    while (isspace((unsigned char)*text)) ++text;
    return *text == '\0';
}

static const char *class_find_ci(const char *text, const char *part)
{
    if (!text || !part || !*part) return NULL;
    for (const char *p = text; *p; ++p)
        if (prefix_ci(p, part)) return p;
    return NULL;
}

static int class_equal_ci_n(const char *a, const char *b, size_t length)
{
    for (size_t i = 0; i < length; ++i)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return 0;
    return 1;
}

static size_t class_reference_variants(const char *text, char variants[][256], size_t cap)
{
    if (!text || cap == 0) return 0;
    size_t count = 1;
    snprintf(variants[0], 256, "%s", text);
    static const char *markers[] = {
        "turma", "sou do ", "sou da ", "estudo no ", "estudo na ",
        "estou no ", "estou na ", "minha sala ", "minha classe ",
        "sou aluno do ", "sou aluna do ", "faco parte do ", "faço parte do ",
        "faco parte da ", "faço parte da ", "estudo em ",
        "atividades do ", "atividades da ", "atividade do ", "atividade da ",
        "tarefas do ", "tarefas da ", "tarefa do ", "tarefa da ",
        "avisos do ", "avisos da ", "aviso do ", "aviso da ",
        "recados do ", "recados da ", "comunicados do ", "comunicados da "
    };
    for (size_t i = 0; i < sizeof markers / sizeof markers[0] && count < cap; ++i) {
        const char *marker = class_find_ci(text, markers[i]);
        if (!marker) continue;
        const char *value = marker + strlen(markers[i]);
        while (*value && (isspace((unsigned char)*value) || *value == ':' ||
                          *value == '=' || *value == '#')) ++value;
        if (prefix_ci(value, "e ")) value += 2;
        else if ((unsigned char)value[0] == 0xC3 && (unsigned char)value[1] == 0xA9 &&
                 isspace((unsigned char)value[2])) value += 3;
        size_t value_len = strcspn(value, ",.!?;\n");
        if (value_len == 0 || value_len >= 256) continue;
        int duplicate = 0;
        for (size_t j = 0; j < count; ++j)
            if (strlen(variants[j]) == value_len &&
                class_equal_ci_n(variants[j], value, value_len)) duplicate = 1;
        if (!duplicate) {
            memcpy(variants[count], value, value_len);
            variants[count][value_len] = '\0';
            ++count;
        }
    }
    return count;
}

/* 1=achou, 2=ambígua, 0=não reconhecida, -1=erro de banco/memória. */
static int resolve_class_reference(const char *text, Class *selected,
                                   char *options, size_t options_cap)
{
    char variants[24][256] = {{0}};
    size_t variant_count = class_reference_variants(text, variants, 24);
    if (!variant_count) return 0;
    char keys[24][256] = {{0}};
    for (size_t i = 0; i < variant_count; ++i)
        (void)normalize_class_reference(variants[i], keys[i], sizeof keys[i]);

    /* Um ID explícito identifica a turma sem depender do nome exibido. */
    int64_t requested_id = 0;
    for (size_t i = 0; i < variant_count; ++i) {
        if (parse_class_id_key(keys[i], &requested_id)) {
            Class found;
            if (classes_find_by_id(requested_id, &found) == SUCCESS &&
                strcmp(found.status, "ACTIVE") == 0) {
                *selected = found;
                return 1;
            }
            return 0;
        }
    }

    Class *classes = NULL;
    size_t class_count = 0;
    if (classes_list(&classes, &class_count) != SUCCESS) return -1;
    Class *matches = calloc(class_count ? class_count : 1, sizeof *matches);
    if (!matches) { free(classes); return -1; }
    size_t match_count = 0;
    for (size_t i = 0; i < class_count; ++i) {
        if (strcmp(classes[i].status, "ACTIVE") != 0) continue;
        char name_key[256] = {0}, grade_key[256] = {0};
        (void)normalize_class_reference(classes[i].name, name_key, sizeof name_key);
        (void)normalize_class_reference(classes[i].grade, grade_key, sizeof grade_key);
        char name_stem[32] = {0}, grade_stem[32] = {0};
        (void)class_numeric_stem(name_key, name_stem, sizeof name_stem);
        (void)class_numeric_stem(grade_key, grade_stem, sizeof grade_stem);
        int matched = 0;
        for (size_t v = 0; v < variant_count && !matched; ++v) {
            if (!keys[v][0]) continue;
            const char *key = keys[v];
            if (key[0] == 'e' && isdigit((unsigned char)key[1])) ++key;
            matched = strcmp(key, name_key) == 0 || strcmp(key, grade_key) == 0;
            if (!matched && !strpbrk(key, "abcdefghijklmnopqrstuvwxyz")) {
                matched = (name_stem[0] && strcmp(key, name_stem) == 0) ||
                          (grade_stem[0] && strcmp(key, grade_stem) == 0);
            }
        }
        if (matched) matches[match_count++] = classes[i];
    }
    free(classes);

    if (match_count == 0) {
        if (is_plain_numeric_reference(text) &&
            parse_numeric_class_id(keys[0], &requested_id)) {
            Class found;
            if (classes_find_by_id(requested_id, &found) == SUCCESS &&
                strcmp(found.status, "ACTIVE") == 0) {
                *selected = found;
                free(matches);
                return 1;
            }
        }
        free(matches);
        return 0;
    }
    if (match_count == 1) {
        *selected = matches[0];
        free(matches);
        return 1;
    }

    if (options && options_cap) {
        size_t used = 0;
        options[0] = '\0';
        for (size_t i = 0; i < match_count; ++i) {
            int n = snprintf(options + used, options_cap - used, "%s%s",
                             i ? ", " : "", matches[i].name);
            if (n < 0 || (size_t)n >= options_cap - used) break;
            used += (size_t)n;
        }
    }
    free(matches);
    return 2;
}

/* Em consultas anônimas da feira, aceite a série após "tarefas do/da" para
 * pedir esclarecimento quando a série tiver mais de uma turma. */
static int resolve_demo_class_query(const char *text, Class *selected,
                                    char *options, size_t options_cap)
{
    int match = resolve_class_reference(text, selected, options, options_cap);
    if (match != 0) return match;
    static const char *markers[] = {
        "tarefas do ", "tarefas da ", "tarefa do ", "tarefa da ",
        "avisos do ", "avisos da ", "aviso do ", "aviso da "
    };
    for (size_t i = 0; i < sizeof markers / sizeof markers[0]; ++i) {
        const char *marker = class_find_ci(text, markers[i]);
        if (!marker) continue;
        const char *reference = marker + strlen(markers[i]);
        size_t length = strcspn(reference, ",.!?;:\n");
        while (length && isspace((unsigned char)reference[length - 1])) --length;
        if (!length || length >= 256) return 0;
        char class_reference[256];
        memcpy(class_reference, reference, length);
        class_reference[length] = '\0';
        return resolve_class_reference(class_reference, selected, options, options_cap);
    }
    return 0;
}

void whatsapp_build_school_context_for_class(int64_t class_id, char *out, size_t cap)
{
    if (!out || cap == 0) return;
    out[0] = '\0';
    char today[11] = "0000-00-00";
    time_t now = time(NULL);
    struct tm local_now;
#if defined(_WIN32)
    if (localtime_s(&local_now, &now) == 0)
#else
    if (localtime_r(&now, &local_now))
#endif
        (void)strftime(today, sizeof today, "%Y-%m-%d", &local_now);

    Class *classes = NULL;
    size_t class_count = 0;
    if (classes_list(&classes, &class_count) != SUCCESS) {
        snprintf(out, cap, "Dados escolares indisponíveis.");
        return;
    }
    int n = snprintf(out, cap, "Hoje é %s.\nTurmas cadastradas:", today);
    if (n < 0 || (size_t)n >= cap) {
        if (cap) out[cap - 1] = '\0';
        free(classes);
        return;
    }
    size_t used = (size_t)n;
    int active_count = 0;
    for (size_t i = 0; i < class_count; ++i) {
        if (strcmp(classes[i].status, "ACTIVE") != 0) continue;
        n = snprintf(out + used, cap - used, " %s (ID %lld);",
                     classes[i].name, (long long)classes[i].id);
        if (n < 0 || (size_t)n >= cap - used) {
            out[cap - 1] = '\0';
            free(classes);
            return;
        }
        used += (size_t)n;
        ++active_count;
    }
    if (!active_count) {
        n = snprintf(out + used, cap - used, " nenhuma turma ativa.");
        if (n < 0 || (size_t)n >= cap - used) {
            out[cap - 1] = '\0';
            free(classes);
            return;
        }
        used += (size_t)n;
    }

    for (size_t i = 0; i < class_count && used + 1 < cap; ++i) {
        if (strcmp(classes[i].status, "ACTIVE") != 0) continue;
        if (class_id <= 0 || classes[i].id != class_id) continue;
        Task *tasks = NULL;
        size_t task_count = 0;
        if (tasks_list_by_class(classes[i].id, false, &tasks, &task_count) != SUCCESS)
            continue;
        int shown = 0;
        for (size_t j = 0; j < task_count && shown < 10; ++j) {
            if (tasks[j].due_date[0] && strcmp(tasks[j].due_date, today) < 0) continue;
            const char *description = tasks[j].description[0]
                ? tasks[j].description : tasks[j].title;
            n = snprintf(out + used, cap - used,
                         "\nTarefa da turma %s: %s - %s (entrega %s)",
                         classes[i].name, tasks[j].subject, description,
                         tasks[j].due_date[0] ? tasks[j].due_date : "sem data");
            if (n < 0 || (size_t)n >= cap - used) {
                out[cap - 1] = '\0';
                free(tasks);
                free(classes);
                return;
            }
            used += (size_t)n;
            ++shown;
        }
        free(tasks);
    }
    free(classes);
}

void whatsapp_build_school_context(char *out, size_t cap)
{
    /* Sem vínculo verificado, o contexto pode listar turmas, mas nunca tarefas. */
    whatsapp_build_school_context_for_class(0, out, cap);
}

static int looks_like_class_reference(const char *text)
{
    if (!text || strlen(text) > 80 || strchr(text, '?') || strchr(text, '\n')) return 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        if (isdigit(*p)) return 1;
    static const char *hints[] = {
        "ano", "serie", "série", "turma", "codigo", "código", "id", "primeir",
        "segund", "terceir", "quart", "quint", "sext", "setim", "sétim",
        "oitav", "non", "decim", "décim"
    };
    for (size_t i = 0; i < sizeof hints / sizeof hints[0]; ++i)
        if (contains_ci(text, hints[i])) return 1;
    return 0;
}


static int anonymous_student_message(const char *phone, const char *body, char *reply, size_t cap)
{
    (void)phone;
    (void)body;
    snprintf(reply, cap,
        "Este numero nao esta cadastrado como aluno. Peça a coordenacao para cadastrar seu nome, telefone e turma.");
    return SUCCESS;
}

static int anonymous_write_request(const char *body)
{
    static const char *verbs[] = {
        "criar", "crie", "cadastrar", "cadastre", "adicionar", "adicione",
        "publicar", "publique", "enviar", "envie", "lancar", "lance",
        "lançar", "lança", "colocar", "coloque", "registrar", "registre",
        "marcar", "marque", "postar", "poste"
    };
    static const char *objects[] = {
        "tarefa", "atividade", "dever", "aviso", "comunicado"
    };
    int has_verb = 0, has_object = 0;
    for (size_t i = 0; i < sizeof verbs / sizeof verbs[0]; ++i)
        if (contains_ci(body, verbs[i])) { has_verb = 1; break; }
    for (size_t i = 0; i < sizeof objects / sizeof objects[0]; ++i)
        if (contains_ci(body, objects[i])) { has_object = 1; break; }
    if (has_verb && has_object) return 1;
    if (contains_ci(body, "comunicado:")) return 1;
    return 0;
}

static int teacher_whatsapp_message(const char *sender_key, int64_t user_id,
                                    const char *body, char *reply, size_t cap)
{
    User user;
    if (users_find_by_id(user_id, &user) != SUCCESS || user.type != USER_TYPE_PROF ||
        user.status != USER_STATUS_ACTIVE) {
        snprintf(reply, cap, "Seu cadastro de professor não está ativo. Procure a coordenação.");
        return SUCCESS;
    }
    int64_t current_teacher_id = 0;
    if (teacher_id(user.id, &current_teacher_id) != SUCCESS) {
        snprintf(reply, cap, "Seu cadastro de professor não foi encontrado. Procure a coordenação.");
        return SUCCESS;
    }

    if (equal_ci(body, "AJUDA") || equal_ci(body, "MENU")) {
        snprintf(reply, cap,
                 "Professor: envie uma tarefa em texto e confirme com SIM. "
                 "MINHAS TAREFAS lista suas tarefas; EDITAR TAREFA <id> | título: ... | entrega: AAAA-MM-DD altera uma. "
                 "LEMBRETE <turma>: <texto> publica um aviso após confirmação; LEMBRETES lista os seus; "
                 "EDITAR LEMBRETE <id>: <texto> altera um. CANCELAR descarta o rascunho.");
        return SUCCESS;
    }

    PendingTask task;
    int has_task = pending_load(sender_key, &task) == SUCCESS;
    sqlite3_stmt *st = NULL;
    int has_notice = 0;
    if (db_prepare("SELECT user_id FROM pending_teacher_notices WHERE from_ref=?;", &st) != SUCCESS)
        return ERROR_DATABASE;
    db_bind_text(st, 1, sender_key);
    has_notice = sqlite3_step(st) == SQLITE_ROW;
    int64_t notice_user = has_notice ? sqlite3_column_int64(st, 0) : 0;
    sqlite3_finalize(st);

    PendingTaskEdit task_edit = {0};
    int has_task_edit = pending_task_edit_load(sender_key, &task_edit) == SUCCESS;
    PendingNoticeEdit notice_edit = {0};
    int has_notice_edit = pending_notice_edit_load(sender_key, &notice_edit) == SUCCESS;

    if (equal_ci(body, "MINHAS TAREFAS") || equal_ci(body, "LISTAR TAREFAS")) {
        teacher_list_tasks(user.id, reply, cap);
        return SUCCESS;
    }
    if (equal_ci(body, "LEMBRETES") || equal_ci(body, "MEUS LEMBRETES") ||
        equal_ci(body, "LISTAR LEMBRETES")) {
        teacher_list_reminders(user.id, reply, cap);
        return SUCCESS;
    }

    int64_t target_id = 0;
    const char *edit_tail = NULL;
    int command = command_target_id(body, "editar tarefa", &target_id, &edit_tail);
    if (command != 0) {
        if (command < 0) {
            snprintf(reply, cap, "Use EDITAR TAREFA <id> | matéria: ... | título: ... | entrega: AAAA-MM-DD.");
            return SUCCESS;
        }
        return teacher_prepare_task_edit(sender_key, user.id, target_id, edit_tail, reply, cap);
    }
    if (prefix_ci(body, "editar tarefa")) {
        snprintf(reply, cap, "Use EDITAR TAREFA <id> | matéria: ... | título: ... | entrega: AAAA-MM-DD.");
        return SUCCESS;
    }
    command = command_target_id(body, "editar lembrete", &target_id, &edit_tail);
    if (command != 0) {
        if (command < 0) {
            snprintf(reply, cap, "Use EDITAR LEMBRETE <id>: novo texto do lembrete.");
            return SUCCESS;
        }
        char new_content[2001];
        if (strlen(edit_tail) > 2000) {
            snprintf(reply, cap, "O novo texto do lembrete pode ter no máximo 2000 caracteres.");
            return SUCCESS;
        }
        snprintf(new_content, sizeof new_content, "%s", edit_tail);
        trim(new_content);
        return teacher_prepare_notice_edit(sender_key, user.id, target_id,
                                           new_content, reply, cap);
    }
    if (prefix_ci(body, "editar lembrete")) {
        snprintf(reply, cap, "Use EDITAR LEMBRETE <id>: novo texto do lembrete.");
        return SUCCESS;
    }

    if (prefix_ci(body, "cancelar")) {
        teacher_pending_clear(sender_key, user.id);
        snprintf(reply, cap, "Publicação pendente cancelada.");
        return SUCCESS;
    }
    if (equal_ci(body, "editar")) {
        teacher_pending_clear(sender_key, user.id);
        snprintf(reply, cap, "Rascunho descartado. Envie novamente os dados corrigidos.");
        return SUCCESS;
    }
    if (equal_ci(body, "sim") || equal_ci(body, "confirmar")) {
        int pending_count = teacher_pending_count(sender_key);
        if (pending_count < 0) {
            snprintf(reply, cap, "Não consegui verificar as publicações pendentes.");
            return SUCCESS;
        }
        if (pending_count > 1) {
            snprintf(reply, cap, "Há mais de uma publicação pendente. Responda CANCELAR para limpar os rascunhos e envie novamente uma por vez.");
            return SUCCESS;
        }
        if (has_task_edit && task_edit.user_id == user.id) {
            teacher_confirm_task_edit(sender_key, user.id, &task_edit, reply, cap);
            return SUCCESS;
        }
        if (has_notice_edit && notice_edit.user_id == user.id) {
            teacher_confirm_notice_edit(sender_key, user.id, &notice_edit, reply, cap);
            return SUCCESS;
        }
        if (has_task) {
            Task saved;
            Class current_class;
            if (task.teacher_id != current_teacher_id ||
                classes_find_by_id(task.class_id, &current_class) != SUCCESS ||
                strcmp(current_class.status, "ACTIVE") != 0 ||
                !classes_teacher_has_access(task.teacher_id, task.class_id)) {
                pending_delete(sender_key);
                snprintf(reply, cap, "A tarefa pendente não está mais autorizada para seu cadastro.");
                return SUCCESS;
            }
            int rc = tasks_create(task.teacher_id, task.class_id, task.subject, task.title,
                                  task.description, task.due_date, &saved);
            if (rc != SUCCESS) {
                snprintf(reply, cap, "Não foi possível publicar a tarefa. Tente novamente.");
                return SUCCESS;
            }
            pending_delete(sender_key);
            snprintf(reply, cap, "Tarefa publicada. Código: %lld. Os alunos podem consultá-la pelo WhatsApp.", (long long)saved.id);
            return SUCCESS;
        }
        if (has_notice && notice_user == user.id) {
            if (db_prepare("SELECT class_id,title,content FROM pending_teacher_notices WHERE from_ref=? AND user_id=?;", &st) != SUCCESS)
                return ERROR_DATABASE;
            db_bind_text(st, 1, sender_key); db_bind_int64(st, 2, (long long)user.id);
            int step = sqlite3_step(st);
            int64_t class_id = step == SQLITE_ROW ? sqlite3_column_int64(st, 0) : 0;
            char title[121] = {0}, content[2001] = {0};
            if (step == SQLITE_ROW) {
                snprintf(title, sizeof title, "%s", (const char *)sqlite3_column_text(st, 1));
                snprintf(content, sizeof content, "%s", (const char *)sqlite3_column_text(st, 2));
            }
            sqlite3_finalize(st);
            int64_t actual_teacher_id = 0;
            Class current_class;
            if (step != SQLITE_ROW || classes_find_by_id(class_id, &current_class) != SUCCESS ||
                strcmp(current_class.status, "ACTIVE") != 0 ||
                teacher_id(user.id, &actual_teacher_id) != SUCCESS ||
                !classes_teacher_has_access(actual_teacher_id, class_id)) {
                snprintf(reply, cap, "O recado pendente não está mais autorizado para essa turma.");
                return SUCCESS;
            }
            if (db_begin() != SUCCESS) return ERROR_DATABASE;
            if (db_prepare("INSERT INTO notices(author_id,class_id,title,content,publish_date) "
                           "VALUES(?,?,?,?,date('now','localtime'));", &st) != SUCCESS) {
                (void)db_rollback(); return ERROR_DATABASE;
            }
            db_bind_int64(st, 1, (long long)user.id); db_bind_int64(st, 2, (long long)class_id);
            db_bind_text(st, 3, title); db_bind_text(st, 4, content);
            int ok = sqlite3_step(st) == SQLITE_DONE; sqlite3_finalize(st);
            if (ok && db_prepare("DELETE FROM pending_teacher_notices WHERE from_ref=? AND user_id=?;", &st) == SUCCESS) {
                db_bind_text(st, 1, sender_key); db_bind_int64(st, 2, (long long)user.id);
                ok = sqlite3_step(st) == SQLITE_DONE; sqlite3_finalize(st);
            } else ok = 0;
            if (!ok || db_commit() != SUCCESS) { (void)db_rollback(); snprintf(reply, cap, "Não foi possível publicar o recado."); return SUCCESS; }
            snprintf(reply, cap, "Recado publicado para a turma. Os alunos podem consultá-lo pelo WhatsApp.");
            return SUCCESS;
        }
        snprintf(reply, cap, "Não há publicação aguardando sua confirmação.");
        return SUCCESS;
    }

    const char *colon = strchr(body, ':');
    if (colon && (prefix_ci(body, "recado") || prefix_ci(body, "aviso") ||
                  prefix_ci(body, "comunicado") || prefix_ci(body, "lembrete"))) {
        int pending_count = teacher_pending_count(sender_key);
        if (pending_count < 0) {
            snprintf(reply, cap, "Não consegui verificar suas publicações pendentes.");
            return SUCCESS;
        }
        if (pending_count > 0) {
            snprintf(reply, cap, "Resolva a publicação pendente com SIM ou CANCELAR antes de iniciar outra.");
            return SUCCESS;
        }
        const char *content = colon + 1;
        while (*content && isspace((unsigned char)*content)) ++content;
        size_t keyword_len = prefix_ci(body, "recado") ? strlen("recado")
            : prefix_ci(body, "aviso") ? strlen("aviso")
            : prefix_ci(body, "lembrete") ? strlen("lembrete") : strlen("comunicado");
        const char *class_start = body + keyword_len;
        while (class_start < colon && isspace((unsigned char)*class_start)) ++class_start;
        size_t class_len = (size_t)(colon - class_start);
        while (class_len && isspace((unsigned char)class_start[class_len - 1])) --class_len;
        char class_reference[256] = {0};
        if (class_len < sizeof class_reference) {
            memcpy(class_reference, class_start, class_len);
            class_reference[class_len] = '\0';
        }
        Class classroom;
        char options[256] = {0};
        int class_match = class_reference[0]
            ? resolve_class_reference(class_reference, &classroom, options, sizeof options) : 0;
        if (!validation_length_between(content, 3, 2000) || class_match != 1) {
            snprintf(reply, cap, "Use LEMBRETE [nome da turma]: texto. Ex.: LEMBRETE 9A: reunião amanhã.");
            return SUCCESS;
        }
        int64_t actual_teacher_id = 0;
        if (teacher_id(user.id, &actual_teacher_id) != SUCCESS ||
            strcmp(classroom.status, "ACTIVE") != 0 ||
            !classes_teacher_has_access(actual_teacher_id, classroom.id)) {
            snprintf(reply, cap, "Você não tem acesso a essa turma.");
            return SUCCESS;
        }
        const char *notice_title = prefix_ci(body, "recado") ? "Recado"
            : prefix_ci(body, "aviso") ? "Aviso"
            : prefix_ci(body, "comunicado") ? "Comunicado" : "Lembrete";
        if (db_prepare("INSERT INTO pending_teacher_notices(from_ref,user_id,class_id,title,content) VALUES(?,?,?,?,?) ON CONFLICT(from_ref) DO NOTHING;", &st) != SUCCESS)
            return ERROR_DATABASE;
        db_bind_text(st, 1, sender_key); db_bind_int64(st, 2, (long long)user.id);
        db_bind_int64(st, 3, (long long)classroom.id); db_bind_text(st, 4, notice_title);
        db_bind_text(st, 5, content);
        int ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(database_get()) == 1;
        sqlite3_finalize(st);
        snprintf(reply, cap, "%s", ok
            ? (strcmp(notice_title, "Lembrete") == 0
                ? "Prévia do lembrete para a turma. Responda SIM para publicar ou CANCELAR para descartar."
                : "Prévia do recado para a turma. Responda SIM para publicar ou CANCELAR para descartar.")
            : "Não consegui preparar o recado ou lembrete.");
        return SUCCESS;
    }

    if (has_notice && notice_user == user.id) {
        snprintf(reply, cap, "Há um recado aguardando confirmação. Responda SIM, EDITAR ou CANCELAR.");
        return SUCCESS;
    }
    return teacher_task(sender_key, user.id, body, reply, cap);
}

int whatsapp_receive_anonymous_message(const char *from_ref, const char *body,
                                       char *reply, size_t cap)
{
    if (!from_ref || !*from_ref || strlen(from_ref) >= 80 || !body || !*body ||
        !reply || cap == 0 || strlen(body) > 4000)
        return ERROR_INVALID_INPUT;
    for (const unsigned char *p = (const unsigned char *)from_ref; *p; ++p)
        if (!(isalnum(*p) || *p == '_' || *p == '-')) return ERROR_INVALID_INPUT;

    if (prefix_ci(body, "vincular")) {
        const char *code = body + strlen("vincular");
        while (*code == ' ' || *code == '\t' || *code == ':' || *code == '-') ++code;
        int link_rc = anonymous_teacher_link_claim(from_ref, code);
        if (link_rc == SUCCESS) {
            snprintf(reply, cap, "WhatsApp vinculado ao seu cadastro de professor. Envie AJUDA para ver como criar e editar tarefas e lembretes.");
        } else if (link_rc == ERROR_NOT_FOUND || link_rc == ERROR_FORBIDDEN || link_rc == ERROR_CONFLICT) {
            snprintf(reply, cap, "Código inválido, expirado ou já utilizado. Gere outro código no SAE e envie: VINCULAR <código>.");
        } else {
            snprintf(reply, cap, "Não consegui validar o vínculo agora. Tente novamente.");
        }
        return SUCCESS;
    }

    int64_t linked_teacher_user_id = 0;
    int teacher_link_rc = anonymous_teacher_link_user(from_ref, &linked_teacher_user_id);
    if (teacher_link_rc == SUCCESS)
        return teacher_whatsapp_message(from_ref, linked_teacher_user_id, body, reply, cap);
    if (teacher_link_rc != ERROR_NOT_FOUND) return teacher_link_rc;

    sqlite3_stmt *st = NULL;
    if (db_prepare("SELECT COALESCE(class_id,0),COALESCE(pending_question,''),COALESCE(class_verified,0) "
                   "FROM anonymous_students WHERE phone=?;", &st) != SUCCESS)
        return ERROR_DATABASE;
    db_bind_text(st, 1, from_ref);
    int exists = sqlite3_step(st) == SQLITE_ROW;
    long long class_id = exists ? sqlite3_column_int64(st, 0) : 0;
    int class_verified = exists ? sqlite3_column_int(st, 2) : 0;
    char pending[4001] = {0};
    if (exists) snprintf(pending, sizeof pending, "%s", (const char *)sqlite3_column_text(st, 1));
    sqlite3_finalize(st);

    /* Alunos sem vínculo autenticado podem consultar informações somente de
     * leitura e escolhem a turma da consulta. Publicações exigem vínculo docente. */
    if (!class_verified) {
        if (anonymous_write_request(body)) {
            snprintf(reply, cap, "Este número não está autorizado a publicar tarefas ou avisos. Peça à coordenação para cadastrar ou autorizar um professor.");
            return SUCCESS;
        }
        int consulta = student_requests_tasks(body) || student_requests_notices(body);
        if (pending[0] && (prefix_ci(body, "cancelar") || prefix_ci(body, "sair"))) {
            if (db_prepare("UPDATE anonymous_students SET pending_question=NULL WHERE phone=?;", &st) != SUCCESS)
                return ERROR_DATABASE;
            db_bind_text(st, 1, from_ref);
            int done = sqlite3_step(st) == SQLITE_DONE;
            sqlite3_finalize(st);
            if (!done) return ERROR_DATABASE;
            snprintf(reply, cap, "Consulta cancelada.");
            return SUCCESS;
        }
        if (consulta || pending[0] || looks_like_class_reference(body)) {
            Class selected;
            char options[256] = {0};
            int match = resolve_demo_class_query(body, &selected, options, sizeof options);
            if (match < 0) return ERROR_DATABASE;
            int64_t selected_id = match == 1 ? selected.id : class_id;
            if (match == 2 || (looks_like_class_reference(body) && match == 0))
                selected_id = 0;
            if (selected_id > 0) {
                if (db_prepare("INSERT INTO anonymous_students(phone,class_id,class_verified,pending_question) "
                               "VALUES(?,?,0,NULL) ON CONFLICT(phone) DO UPDATE SET "
                               "class_id=excluded.class_id,pending_question=NULL,updated_at=datetime('now');", &st) != SUCCESS)
                    return ERROR_DATABASE;
                db_bind_text(st, 1, from_ref);
                db_bind_int64(st, 2, selected_id);
                int done = sqlite3_step(st) == SQLITE_DONE;
                sqlite3_finalize(st);
                if (!done) return ERROR_DATABASE;
                const char *question = consulta ? body : (pending[0] ? pending : "tarefas");
                char query[128];
                snprintf(query, sizeof query, "%s%s%s%s",
                         student_requests_notices(question) ? "avisos" : "tarefas",
                         contains_ci(question, "hoje") ? " hoje" : "",
                         (contains_ci(question, "amanha") || contains_ci(question, "amanhã")) ? " amanha" : "",
                         contains_ci(question, "semana") ? " semana" : "");
                return student_message(query, selected_id, reply, cap);
            }
            if (db_prepare("INSERT INTO anonymous_students(phone,class_verified,pending_question) "
                           "VALUES(?,0,?) ON CONFLICT(phone) DO UPDATE SET "
                           "pending_question=excluded.pending_question,updated_at=datetime('now');", &st) != SUCCESS)
                return ERROR_DATABASE;
            db_bind_text(st, 1, from_ref);
            db_bind_text(st, 2, consulta ? body : (pending[0] ? pending : "tarefas"));
            int done = sqlite3_step(st) == SQLITE_DONE;
            sqlite3_finalize(st);
            if (!done) return ERROR_DATABASE;
            if (match == 2)
                snprintf(reply, cap, "Encontrei mais de uma turma: %s. Envie o nome completo.", options);
            else if (looks_like_class_reference(body))
                snprintf(reply, cap, "Essa turma ainda não está cadastrada. Peça à coordenação para cadastrá-la.");
            else
                snprintf(reply, cap, "De qual turma você quer consultar tarefas ou avisos? Envie o nome da turma, por exemplo 9A.");
            return SUCCESS;
        }
        return student_message(body, 0, reply, cap);
    }

    /* Older installs may retain a pending class-selection prompt. Never use a
     * free-form WhatsApp reply to change a verified student's class. */
    if (pending[0]) {
        if (db_prepare("UPDATE anonymous_students SET pending_question=NULL,updated_at=datetime('now') WHERE phone=?;", &st) != SUCCESS)
            return ERROR_DATABASE;
        db_bind_text(st, 1, from_ref);
        int cleared = sqlite3_step(st) == SQLITE_DONE;
        sqlite3_finalize(st);
        if (!cleared) return ERROR_DATABASE;
    }
    if (class_id <= 0) {
        snprintf(reply, cap, "Informe o nome da turma para consultar tarefas e avisos, por exemplo 9A.");
        return SUCCESS;
    }

    return student_message(body, class_id, reply, cap);
}

int whatsapp_store_anonymous_inbox_message(const char *message_id,
                                           const char *from_ref,
                                           const char *body,
                                           const char *kind)
{
    if (!message_id || !*message_id || strlen(message_id) >= 64 ||
        !from_ref || !*from_ref || strlen(from_ref) >= 80 ||
        !body || !*body || strlen(body) > 4000)
        return ERROR_INVALID_INPUT;
    const char *type = "OTHER";
    if (kind && strcmp(kind, "text") == 0) type = "TEXT";
    else if (kind && strcmp(kind, "audio") == 0) type = "AUDIO";
    else if (kind && strcmp(kind, "image") == 0) type = "IMAGE";
    sqlite3_stmt *st = NULL;
    if (db_prepare("INSERT OR IGNORE INTO messages(external_id,sender_phone,user_type,content,msg_type,status) "
                   "VALUES(?,?,'ANONYMOUS',?,?, 'RECEIVED');", &st) != SUCCESS)
        return ERROR_DATABASE;
    db_bind_text(st, 1, message_id);
    db_bind_text(st, 2, from_ref);
    db_bind_text(st, 3, body);
    db_bind_text(st, 4, type);
    int saved = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return saved ? SUCCESS : ERROR_DATABASE;
}

int whatsapp_mark_anonymous_inbox_message(const char *message_id, const char *status)
{
    if (!message_id || !*message_id || strlen(message_id) >= 64 ||
        !status || (strcmp(status, "PROCESSED") != 0 && strcmp(status, "FAILED") != 0 &&
                    strcmp(status, "IGNORED") != 0))
        return ERROR_INVALID_INPUT;
    sqlite3_stmt *st = NULL;
    if (db_prepare("UPDATE messages SET status=? WHERE external_id=?;", &st) != SUCCESS)
        return ERROR_DATABASE;
    db_bind_text(st, 1, status);
    db_bind_text(st, 2, message_id);
    int saved = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return saved ? SUCCESS : ERROR_DATABASE;
}

static int coordinator_notice_message(const char *phone, int64_t author_id,
                                     const char *body, char *reply, size_t cap)
{
    if (prefix_ci(body, "comunicado:") || prefix_ci(body, "aviso:")) {
        const char *content = strchr(body, ':') + 1;
        while (*content && isspace((unsigned char)*content)) ++content;
        if (!validation_length_between(content, 3, 2000)) {
            snprintf(reply, cap, "Escreva o texto do comunicado apos COMUNICADO:."); return SUCCESS;
        }
        sqlite3_stmt *st = NULL;
        if (db_prepare("INSERT INTO pending_notices(phone,title,content) VALUES(?,'Comunicado',?) "
                       "ON CONFLICT(phone) DO UPDATE SET content=excluded.content,updated_at=datetime('now');", &st) != SUCCESS) {
            snprintf(reply, cap, "Nao consegui preparar o comunicado."); return SUCCESS;
        }
        db_bind_text(st, 1, phone); db_bind_text(st, 2, content);
        int ok = sqlite3_step(st) == SQLITE_DONE; sqlite3_finalize(st);
        snprintf(reply, cap, "%s. Para qual turma? Responda com o nome da turma ou TODAS.",
                 ok ? "Comunicado recebido" : "Nao consegui preparar o comunicado");
        return SUCCESS;
    }
    sqlite3_stmt *st = NULL;
    if (db_prepare("SELECT title,content FROM pending_notices WHERE phone=?;", &st) != SUCCESS) {
        snprintf(reply, cap, "Use COMUNICADO: seguido do texto para publicar um aviso."); return SUCCESS;
    }
    db_bind_text(st, 1, phone);
    if (sqlite3_step(st) != SQLITE_ROW) {
        sqlite3_finalize(st);
        snprintf(reply, cap, "Use COMUNICADO: seguido do texto para publicar um aviso."); return SUCCESS;
    }
    char title[121], content[2001];
    snprintf(title, sizeof title, "%s", (const char *)sqlite3_column_text(st, 0));
    snprintf(content, sizeof content, "%s", (const char *)sqlite3_column_text(st, 1));
    sqlite3_finalize(st);
    int64_t class_id = 0; Class classroom;
    int all = equal_ci(body, "todas") || equal_ci(body, "todos");
    if (!all) {
        if (classes_find_by_name(body, &classroom) != SUCCESS ||
            strcmp(classroom.status, "ACTIVE") != 0) {
            snprintf(reply, cap, "Nao encontrei essa turma ativa. Informe o nome exato ou responda TODAS."); return SUCCESS;
        }
        class_id = classroom.id;
    }
    if (db_prepare("INSERT INTO notices(author_id,class_id,title,content) VALUES(?,?,?,?);", &st) != SUCCESS) {
        snprintf(reply, cap, "Nao consegui salvar o comunicado."); return SUCCESS;
    }
    db_bind_int64(st, 1, (long long)author_id);
    if (all) db_bind_null(st, 2); else db_bind_int64(st, 2, (long long)class_id);
    db_bind_text(st, 3, title); db_bind_text(st, 4, content);
    int ok = sqlite3_step(st) == SQLITE_DONE; sqlite3_finalize(st);
    if (db_prepare("DELETE FROM pending_notices WHERE phone=?;", &st) == SUCCESS) {
        db_bind_text(st, 1, phone); (void)sqlite3_step(st); sqlite3_finalize(st);
    }
    snprintf(reply, cap, "%s", ok ? "Comunicado publicado." : "Nao consegui salvar o comunicado.");
    return SUCCESS;
}

int whatsapp_receive_message(const char *phone, const char *body, char *reply, size_t cap)
{
    if (reply && cap) reply[0] = '\0';
    if (!phone || !validation_is_valid_phone(phone)) return ERROR_INVALID_PHONE;
    if (!body || !reply || cap < 16 || !validation_length_between(body,1,2000)) return ERROR_INVALID_INPUT;
    User user;
    int found_user = users_find_by_phone(phone, &user) == SUCCESS && user.status == USER_STATUS_ACTIVE;
    if (!found_user) return anonymous_student_message(phone, body, reply, cap);
    if (user.type == USER_TYPE_PROF) {
        if (equal_ci(body,"cancelar")) { pending_delete(phone); snprintf(reply,cap,"Tarefa pendente cancelada."); return SUCCESS; }
        if (equal_ci(body,"editar")) { pending_delete(phone); snprintf(reply,cap,"Tarefa pendente descartada para edicao. Envie novamente os dados corrigidos."); return SUCCESS; }
        if (equal_ci(body,"sim") || equal_ci(body,"confirmar")) {
            PendingTask task; Task saved;
            if (pending_load(phone,&task) != SUCCESS) { snprintf(reply,cap,"Nao ha tarefa aguardando confirmacao."); return SUCCESS; }
            int64_t current_teacher_id = 0;
            if (teacher_id(user.id, &current_teacher_id) != SUCCESS || current_teacher_id != task.teacher_id) {
                snprintf(reply,cap,"Esta tarefa pendente nao pertence ao seu cadastro de professor.");
                return SUCCESS;
            }
            if (tasks_create(task.teacher_id,task.class_id,task.subject,task.title,task.description,task.due_date,&saved) != SUCCESS) { snprintf(reply,cap,"Nao foi possivel publicar a tarefa."); return SUCCESS; }
            pending_delete(phone); snprintf(reply,cap,"Tarefa publicada com sucesso. Codigo: %lld.",(long long)saved.id); return SUCCESS;
        }
        return teacher_task(phone,user.id,body,reply,cap);
    }
    if (user.type == USER_TYPE_STUD) {
        sqlite3_stmt *student = NULL;
        if (db_prepare("SELECT class_id FROM students WHERE user_id=?;", &student) != SUCCESS) {
            snprintf(reply, cap, "Nao consegui verificar seu cadastro agora. Tente novamente.");
            return SUCCESS;
        }
        db_bind_int64(student, 1, (long long)user.id);
        int linked = sqlite3_step(student) == SQLITE_ROW &&
            sqlite3_column_type(student, 0) != SQLITE_NULL;
        int64_t class_id = linked ? sqlite3_column_int64(student, 0) : 0;
        sqlite3_finalize(student);
        Class classroom;
        if (class_id <= 0 || classes_find_by_id(class_id, &classroom) != SUCCESS ||
            strcmp(classroom.status, "ACTIVE") != 0) {
            snprintf(reply, cap, "Seu cadastro ainda nao esta associado a uma turma ativa. Procure a coordenacao.");
            return SUCCESS;
        }
        return student_message(body, class_id, reply, cap);
    }
    return coordinator_notice_message(phone, user.id, body, reply, cap);
}
