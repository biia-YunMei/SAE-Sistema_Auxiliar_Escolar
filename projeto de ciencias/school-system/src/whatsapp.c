/* Canal WhatsApp: entrega, identificacao do usuario e comandos escolares. */
#include "whatsapp.h"
#include "classes.h"
#include "database.h"
#include "logger.h"
#include "tasks.h"
#include "users.h"
#include "validation.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(WHATSAPP_USE_CURL)
#include <curl/curl.h>
#endif

int whatsapp_send_via_api(const char *phone, const char *text);

typedef struct {
    int64_t teacher_id, class_id;
    char subject[64], title[121], description[1024], due_date[11];
} PendingTask;

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
    if (!text || !validation_length_between(text, 1, 4000)) return ERROR_INVALID_INPUT;
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

static int student_class(int64_t user_id, int64_t *out, Class *classroom)
{
    sqlite3_stmt *st = NULL;
    if (db_prepare("SELECT class_id FROM students WHERE user_id=?;", &st) != SUCCESS) return ERROR_DATABASE;
    db_bind_int64(st, 1, (long long)user_id);
    int found = sqlite3_step(st) == SQLITE_ROW;
    if (found) *out = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return (!found || *out <= 0) ? ERROR_NOT_FOUND : classes_find_by_id(*out, classroom);
}

static int pending_save(const char *phone, const PendingTask *task)
{
    sqlite3_stmt *st = NULL;
    int rc = db_prepare("INSERT INTO pending_tasks(phone,teacher_id,class_id,subject,title,description,due_date) "
                        "VALUES(?,?,?,?,?,?,?) ON CONFLICT(phone) DO UPDATE SET "
                        "teacher_id=excluded.teacher_id,class_id=excluded.class_id,subject=excluded.subject,"
                        "title=excluded.title,description=excluded.description,due_date=excluded.due_date,updated_at=datetime('now');", &st);
    if (rc != SUCCESS) return rc;
    db_bind_text(st,1,phone); db_bind_int64(st,2,task->teacher_id); db_bind_int64(st,3,task->class_id);
    db_bind_text(st,4,task->subject); db_bind_text(st,5,task->title); db_bind_text(st,6,task->description); db_bind_text(st,7,task->due_date);
    int result = sqlite3_step(st); sqlite3_finalize(st);
    return result == SQLITE_DONE ? SUCCESS : ERROR_DATABASE;
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
    PendingTask task = {0}; char class_name[64] = {0}; Class classroom;
    if (!field(body,"turma",class_name,sizeof class_name) || !field(body,"materia",task.subject,sizeof task.subject) ||
        !field(body,"titulo",task.title,sizeof task.title) || !field(body,"entrega",task.due_date,sizeof task.due_date)) {
        snprintf(reply,cap,"Use: tarefa | turma: NOME | materia: MATERIA | titulo: TITULO | entrega: AAAA-MM-DD | descricao: OPCIONAL"); return SUCCESS;
    }
    (void)field(body,"descricao",task.description,sizeof task.description);
    if (!validation_is_valid_date(task.due_date)) { snprintf(reply,cap,"A entrega deve usar AAAA-MM-DD."); return SUCCESS; }
    if (teacher_id(user_id,&task.teacher_id) != SUCCESS || classes_find_by_name(class_name,&classroom) != SUCCESS) {
        snprintf(reply,cap,"Professor ou turma nao encontrado."); return SUCCESS;
    }
    task.class_id=classroom.id;
    if (!classes_teacher_has_access(task.teacher_id,task.class_id)) { snprintf(reply,cap,"Voce nao esta vinculado a turma %s.",classroom.name); return SUCCESS; }
    if (pending_save(phone,&task) != SUCCESS) { snprintf(reply,cap,"Nao consegui preparar a tarefa."); return SUCCESS; }
    snprintf(reply,cap,"Resumo: %s | %s | %s | entrega: %s. Responda CONFIRMAR ou CANCELAR.",classroom.name,task.subject,task.title,task.due_date);
    return SUCCESS;
}

static int student_tasks(int64_t user_id, char *reply, size_t cap)
{
    int64_t class_id=0; Class classroom; Task *list=NULL; size_t count=0;
    if (student_class(user_id,&class_id,&classroom) != SUCCESS) { snprintf(reply,cap,"Seu numero ainda nao esta vinculado a uma turma. Procure a coordenacao."); return SUCCESS; }
    if (tasks_list_by_class(class_id,false,&list,&count) != SUCCESS) { snprintf(reply,cap,"Nao consegui consultar as tarefas."); return SUCCESS; }
    size_t used=(size_t)snprintf(reply,cap,"Tarefas de %s:\n",classroom.name);
    if (!count) snprintf(reply+used,cap-used,"Nenhuma tarefa ativa.");
    for (size_t i=0;i<count && i<6 && used+180<cap;i++) {
        int n=snprintf(reply+used,cap-used,"%zu. %s - %s (%s)\n",i+1,list[i].subject,list[i].title,list[i].due_date);
        if (n < 0) break;
        used += (size_t)n;
    }
    free(list); return SUCCESS;
}

static int student_notices(int64_t user_id, char *reply, size_t cap)
{
    int64_t class_id=0; Class classroom;
    if (student_class(user_id,&class_id,&classroom) != SUCCESS) {
        snprintf(reply,cap,"Seu numero ainda nao esta vinculado a uma turma. Procure a coordenacao.");
        return SUCCESS;
    }
    sqlite3_stmt *st = NULL;
    if (db_prepare("SELECT title,content FROM notices WHERE status='ACTIVE' AND "
                   "(class_id IS NULL OR class_id=?) ORDER BY id DESC LIMIT 5;", &st) != SUCCESS) {
        snprintf(reply,cap,"Nao consegui consultar os recados.");
        return SUCCESS;
    }
    db_bind_int64(st,1,(long long)class_id);
    size_t used=(size_t)snprintf(reply,cap,"Recados de %s:\n",classroom.name);
    int count=0;
    while (sqlite3_step(st) == SQLITE_ROW && used + 220 < cap) {
        const char *title=(const char *)sqlite3_column_text(st,0);
        const char *content=(const char *)sqlite3_column_text(st,1);
        int n=snprintf(reply+used,cap-used,"- %s: %.140s\n",title?title:"Recado",content?content:"");
        if (n < 0) break;
        used+=(size_t)n;
        ++count;
    }
    sqlite3_finalize(st);
    if (!count) snprintf(reply+used,cap-used,"Nenhum recado ativo.");
    return SUCCESS;
}

int whatsapp_receive_message(const char *phone, const char *body, char *reply, size_t cap)
{
    if (!phone || !validation_is_valid_phone(phone)) return ERROR_INVALID_PHONE;
    if (!body || !reply || cap < 16 || !validation_length_between(body,1,2000)) return ERROR_INVALID_INPUT;
    User user;
    if (users_find_by_phone(phone,&user) != SUCCESS || user.status != USER_STATUS_ACTIVE) {
        snprintf(reply,cap,"Este numero nao esta autorizado no SAE. Procure a coordenacao."); return SUCCESS;
    }
    if (user.type == USER_TYPE_PROF) {
        if (equal_ci(body,"cancelar")) { pending_delete(phone); snprintf(reply,cap,"Tarefa pendente cancelada."); return SUCCESS; }
        if (equal_ci(body,"confirmar")) {
            PendingTask task; Task saved;
            if (pending_load(phone,&task) != SUCCESS) { snprintf(reply,cap,"Nao ha tarefa aguardando confirmacao."); return SUCCESS; }
            if (tasks_create(task.teacher_id,task.class_id,task.subject,task.title,task.description,task.due_date,&saved) != SUCCESS) { snprintf(reply,cap,"Nao foi possivel publicar a tarefa."); return SUCCESS; }
            pending_delete(phone); snprintf(reply,cap,"Tarefa publicada com sucesso. Codigo: %lld.",(long long)saved.id); return SUCCESS;
        }
        if (prefix_ci(body,"tarefa")) return teacher_task(phone,user.id,body,reply,cap);
        snprintf(reply,cap,"Envie TAREFA para cadastrar uma atividade ou CONFIRMAR para publicar a pendente."); return SUCCESS;
    }
    if (user.type == USER_TYPE_STUD) {
        if (equal_ci(body,"tarefas")) return student_tasks(user.id,reply,cap);
        if (equal_ci(body,"recados")) return student_notices(user.id,reply,cap);
        snprintf(reply,cap,"Ola, %s. Envie TAREFAS ou RECADOS para consultar sua turma.",user.name); return SUCCESS;
    }
    snprintf(reply,cap,"Coordenacao: use o painel ou a CLI para administrar o SAE."); return SUCCESS;
}
