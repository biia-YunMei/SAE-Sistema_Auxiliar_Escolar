/* Canal WhatsApp: entrega, identificacao do usuario e comandos escolares. */
#include "whatsapp.h"
#include "classes.h"
#include "ai.h"
#include "database.h"
#include "logger.h"
#include "tasks.h"
#include "users.h"
#include "validation.h"

#include <ctype.h>
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

static int class_mentioned(const char *body, Class *classroom);

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

static int teacher_task(const char *phone, int64_t user_id, const char *body, char *reply, size_t cap)
{
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
    if (classes_find_by_name(class_name,&classroom) != SUCCESS) {
        /* Na Fase 0 a coordenação não pré-vincula turmas aos professores.
         * Turmas novas podem ser criadas pelo professor ao enviar a tarefa. */
        if (!validation_length_between(class_name, 1, 63) ||
            classes_create(class_name, class_name, SHIFT_MORNING, &classroom) != SUCCESS) {
            snprintf(reply,cap,"Nao consegui identificar ou criar a turma %s. Confira o nome e tente novamente.",class_name); return SUCCESS;
        }
    }
    if (strcmp(classroom.status, "ACTIVE") != 0) {
        snprintf(reply,cap,"A turma %s nao esta ativa.",class_name); return SUCCESS;
    }
    task.class_id=classroom.id;
    if (!classes_teacher_has_access(task.teacher_id,task.class_id)) {
        if (classes_link_teacher(task.teacher_id, task.class_id) != SUCCESS) {
            snprintf(reply,cap,"Nao consegui associar voce a turma %s.",classroom.name); return SUCCESS;
        }
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
    size_t used = (size_t)snprintf(reply, cap, "Tarefas de %s:\n", classroom->name);
    int shown = 0;
    if (!count) used += (size_t)snprintf(reply + used, cap - used, "Nenhuma tarefa ativa.");
    for (size_t i = 0; i < count && used + 180 < cap; ++i) {
        const char *due = list[i].due_date;
        if ((exact_tomorrow && strcmp(due, tomorrow) != 0) ||
            (today_only && strcmp(due, today) != 0) ||
            (week && (strcmp(due, today) < 0 || strcmp(due, week_end) > 0)) ||
            (!exact_tomorrow && !today_only && !week && strcmp(due, today) < 0)) continue;
        if (shown >= 8) break;
        int n = snprintf(reply + used, cap - used, "- %s: %s (entrega %s)\n",
            list[i].subject, list[i].description[0] ? list[i].description : list[i].title,
            due);
        if (n < 0) break;
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
    if (assigned_class_id <= 0 || classes_find_by_id(assigned_class_id, &classroom) != SUCCESS) {
        snprintf(reply, cap, "De qual turma voce e? (ex.: 9A, 1o EM)");
        return SUCCESS;
    }
    const char *question = body;
    if (contains_ci(question, "tarefa") || contains_ci(question, "atividade") ||
        prefix_ci(question, "tarefas") || contains_ci(question, "entregar"))
        return student_tasks_for_class(assigned_class_id, &classroom, question, reply, cap);
    if (prefix_ci(question, "recados") || prefix_ci(question, "comunicados") ||
        contains_ci(question, "aviso")) {
        sqlite3_stmt *st = NULL;
        if (db_prepare("SELECT title,content FROM notices WHERE status='ACTIVE' AND (class_id IS NULL OR class_id=?) ORDER BY id DESC LIMIT 5;", &st) != SUCCESS) {
            snprintf(reply, cap, "Nao consegui consultar os comunicados agora."); return SUCCESS;
        }
        db_bind_int64(st, 1, (long long)assigned_class_id);
        size_t used = (size_t)snprintf(reply, cap, "Comunicados de %s:\n", classroom.name);
        int count = 0;
        while (sqlite3_step(st) == SQLITE_ROW && used + 180 < cap) {
            int n = snprintf(reply + used, cap - used, "- %s: %s\n",
                (const char *)sqlite3_column_text(st, 0), (const char *)sqlite3_column_text(st, 1));
            if (n < 0) break;
            used += (size_t)n;
            ++count;
        }
        sqlite3_finalize(st);
        if (!count) snprintf(reply + used, cap - used, "Nenhum comunicado ativo.");
        return SUCCESS;
    }
    Task *tasks = NULL; size_t count = 0; char context[900] = {0};
    if (tasks_list_by_class(assigned_class_id, false, &tasks, &count) == SUCCESS && count) {
        size_t used = 0;
        for (size_t i = 0; i < count && used + 100 < sizeof context; ++i)
            used += (size_t)snprintf(context + used, sizeof context - used, "%s%s: %s", i ? "; " : "", tasks[i].subject, tasks[i].title);
    }
    free(tasks);
    (void)ai_answer_student_question(question, context, reply, cap);
    return SUCCESS;
}

static int anonymous_class_load(const char *phone, int64_t *class_id, char *pending, size_t pending_cap)
{
    sqlite3_stmt *st = NULL;
    if (db_prepare("SELECT class_id,pending_question FROM anonymous_students WHERE phone=?;", &st) != SUCCESS)
        return ERROR_DATABASE;
    db_bind_text(st, 1, phone);
    int found = sqlite3_step(st) == SQLITE_ROW;
    if (found) {
        *class_id = sqlite3_column_type(st, 0) == SQLITE_NULL ? 0 : sqlite3_column_int64(st, 0);
        const unsigned char *question = sqlite3_column_text(st, 1);
        if (pending && pending_cap) snprintf(pending, pending_cap, "%s", question ? (const char *)question : "");
    }
    sqlite3_finalize(st);
    return found ? SUCCESS : ERROR_NOT_FOUND;
}

static int anonymous_class_save(const char *phone, int64_t class_id, const char *pending)
{
    sqlite3_stmt *st = NULL;
    if (db_prepare("INSERT INTO anonymous_students(phone,class_id,pending_question) VALUES(?,?,?) "
                   "ON CONFLICT(phone) DO UPDATE SET class_id=excluded.class_id,"
                   "pending_question=excluded.pending_question,updated_at=datetime('now');", &st) != SUCCESS)
        return ERROR_DATABASE;
    db_bind_text(st, 1, phone);
    if (class_id > 0) db_bind_int64(st, 2, (long long)class_id); else db_bind_null(st, 2);
    if (pending && *pending) db_bind_text(st, 3, pending); else db_bind_null(st, 3);
    int ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok ? SUCCESS : ERROR_DATABASE;
}

static int looks_like_class_name(const char *text)
{
    size_t len = strlen(text);
    if (!len || len > 31 || strchr(text, '?') || contains_ci(text, "tarefa") ||
        contains_ci(text, "atividade") || contains_ci(text, "como ") ||
        contains_ci(text, "duvida") || contains_ci(text, "dúvida")) return 0;
    int digit = 0, alpha = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (isdigit(*p)) digit = 1;
        if (isalpha(*p)) alpha = 1;
        if (!(isalnum(*p) || isspace(*p) || *p >= 0x80 || *p == '-' || *p == '.')) return 0;
    }
    return digit && alpha;
}

static int student_task_submission(const char *body)
{
    int assignment_content = contains_ci(body, "pagina") || contains_ci(body, "página") ||
        contains_ci(body, "exercicio") || contains_ci(body, "exercício") ||
        contains_ci(body, "fazer ") || contains_ci(body, "ler ") ||
        contains_ci(body, "redacao") || contains_ci(body, "redação") ||
        contains_ci(body, "trabalho");
    return prefix_ci(body, "tarefa de aluno") || prefix_ci(body, "minha tarefa:") ||
        contains_ci(body, "criar tarefa") || contains_ci(body, "cadastrar tarefa") ||
        contains_ci(body, "entrega: ") ||
        (contains_ci(body, "entrega") &&
         (contains_ci(body, "matem") || contains_ci(body, "portugu") ||
          contains_ci(body, "hist") || contains_ci(body, "cien") ||
          contains_ci(body, "ingl")) && assignment_content);
}

static int resolve_student_class(const char *body, Class *classroom)
{
    if (class_mentioned(body, classroom) && strcmp(classroom->status, "ACTIVE") == 0) return SUCCESS;
    char candidate[64];
    snprintf(candidate, sizeof candidate, "%s", body);
    trim(candidate);
    /* A turma pode vir no começo da pergunta ("9A tem tarefa?"). */
    char *end = candidate;
    while (*end && !isspace((unsigned char)*end) && *end != ',' && *end != ':' && *end != '?') ++end;
    if (*end) *end = '\0';
    if (!looks_like_class_name(candidate)) return ERROR_NOT_FOUND;
    if (classes_find_by_name(candidate, classroom) == SUCCESS)
        return strcmp(classroom->status, "ACTIVE") == 0 ? SUCCESS : ERROR_NOT_FOUND;
    return classes_create(candidate, candidate, SHIFT_MORNING, classroom);
}

static int anonymous_student_message(const char *phone, const char *body, char *reply, size_t cap)
{
    if (student_task_submission(body)) {
        snprintf(reply, cap, "Este numero nao esta autorizado como professor. Alunos nao podem cadastrar tarefas; consulte as atividades ou procure a coordenacao.");
        return SUCCESS;
    }
    int64_t class_id = 0;
    char pending[2001] = {0};
    (void)anonymous_class_load(phone, &class_id, pending, sizeof pending);
    if (class_id <= 0) {
        Class classroom;
        if (resolve_student_class(body, &classroom) != SUCCESS) {
            const char *question = pending[0] ? pending : body;
            if (anonymous_class_save(phone, 0, question) != SUCCESS) {
                snprintf(reply, cap, "Nao consegui guardar sua conversa agora. Tente novamente.");
                return SUCCESS;
            }
            snprintf(reply, cap, "De qual turma voce e? (ex.: 9A, 1o EM)");
            return SUCCESS;
        }
        class_id = classroom.id;
        if (anonymous_class_save(phone, class_id, NULL) != SUCCESS) {
            snprintf(reply, cap, "Nao consegui guardar sua turma. Tente novamente.");
            return SUCCESS;
        }
        const char *question = pending[0] ? pending : body;
        if (pending[0] || !class_mentioned(body, &classroom))
            return student_message(question, class_id, reply, cap);
        /* Se a primeira mensagem já incluía uma turma, processe-a agora. */
        return student_message(body, class_id, reply, cap);
    }
    Class mentioned;
    if (class_mentioned(body, &mentioned) && strcmp(mentioned.status, "ACTIVE") == 0 && mentioned.id != class_id) {
        class_id = mentioned.id;
        (void)anonymous_class_save(phone, class_id, NULL);
    }
    return student_message(body, class_id, reply, cap);
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
    if (user.type == USER_TYPE_STUD) return anonymous_student_message(phone, body, reply, cap);
    return coordinator_notice_message(phone, user.id, body, reply, cap);
}
