/* tasks_controller.c
 * Endpoints:
 *   GET    /api/tasks?class_id=...   — listar por turma
 *   POST   /api/tasks                — criar
 *   PUT    /api/tasks/:id            — editar
 *   DELETE /api/tasks/:id            — excluir
 */
#include "http_server.h"
#include "auth.h"
#include "tasks.h"
#include "users.h"
#include "database.h"
#include "validation.h"
#include "logger.h"
#include "controllers.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* extrai "key":"value" ou "key":number do body JSON ingênuo */
static int extract_str(const char *body, const char *key, char *out, size_t cap)
{
    char needle[64]; int n = snprintf(needle, sizeof needle, "\"%s\":\"", key);
    if (n < 0 || (size_t)n >= sizeof needle) return 0;
    const char *p = strstr(body, needle);
    if (!p) return 0;
    p += n;
    const char *e = strchr(p, '"');
    if (!e) return 0;
    size_t len = (size_t)(e - p);
    if (len >= cap) len = cap - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return 1;
}

static long long extract_int(const char *body, const char *key, long long defv)
{
    char needle[64]; int n = snprintf(needle, sizeof needle, "\"%s\":", key);
    if (n < 0 || (size_t)n >= sizeof needle) return defv;
    const char *p = strstr(body, needle);
    if (!p) return defv;
    p += n;
    while (*p == ' ' || *p == '\t') p++;
    char *end = NULL;
    long long v = strtoll(p, &end, 10);
    if (end == p) return defv;
    return v;
}

static int get_query_param(const char *query, const char *name,
                           char *out, size_t cap)
{
    if (!query) return 0;
    size_t nlen = strlen(name);
    const char *p = query;
    while (*p) {
        if (strncmp(p, name, nlen) == 0 && p[nlen] == '=') {
            p += nlen + 1;
            const char *e = strchr(p, '&');
            if (!e) e = p + strlen(p);
            size_t n = (size_t)(e - p);
            if (n >= cap) n = cap - 1;
            memcpy(out, p, n);
            out[n] = '\0';
            return 1;
        }
        const char *e = strchr(p, '&');
        if (!e) break;
        p = e + 1;
    }
    return 0;
}

/* resolve sessão via Authorization; se falhar, devolve 401. */
static int require_session(const HttpRequest *req, HttpResponse *res, AuthSession *s)
{
    if (!req->auth)
        return http_respond_error(res, 401, "missing authorization");
    int rc = auth_session_resolve(req->auth, s);
    if (rc != SUCCESS) {
        http_respond_error(res, 401, "invalid session");
        return rc;
    }
    return SUCCESS;
}

static int teacher_id_for_session(const AuthSession *s, long long *out)
{
    sqlite3_stmt *st = NULL;
    if (!s || !out || s->type != USER_TYPE_PROF ||
        db_prepare("SELECT id FROM teachers WHERE user_id=?;", &st) != SUCCESS)
        return ERROR_FORBIDDEN;
    db_bind_int64(st, 1, (long long)s->user_id);
    int rc = sqlite3_step(st) == SQLITE_ROW ? SUCCESS : ERROR_NOT_FOUND;
    if (rc == SUCCESS) *out = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return rc;
}

int ctrl_tasks_list(const HttpRequest *req, HttpResponse *res)
{
    AuthSession s;
    int rc = require_session(req, res, &s);
    if (rc != SUCCESS) return rc;

    char qbuf[256] = {0};
    if (!get_query_param(req->query, "class_id", qbuf, sizeof qbuf))
        return http_respond_error(res, 400, "class_id required");
    long long class_id = strtoll(qbuf, NULL, 10);

    Task *list = NULL; size_t n = 0;
    rc = tasks_list_by_class((int64_t)class_id, false, &list, &n);
    if (rc != SUCCESS) { return http_respond_error(res, 500, "list failed"); }

    /* Renderiza JSON manualmente. */
    size_t cap = 256 + n * 2048;
    char *out = (char *)malloc(cap);
    if (!out) { free(list); return http_respond_error(res, 500, "oom"); }
    size_t p = 0;
    p += (size_t)snprintf(out + p, cap - p, "{\"count\":%zu,\"items\":[", n);
    for (size_t i = 0; i < n; i++) {
        p += (size_t)snprintf(out + p, cap - p,
            "%s{\"id\":%lld,\"teacher_id\":%lld,\"class_id\":%lld,"
            "\"subject\":\"%s\",\"title\":\"%s\",\"description\":\"%s\","
            "\"due_date\":\"%s\",\"status\":\"%s\"}",
            i ? "," : "",
            (long long)list[i].id, (long long)list[i].teacher_id,
            (long long)list[i].class_id,
            list[i].subject, list[i].title, list[i].description,
            list[i].due_date, task_status_str(list[i].status));
    }
    p += (size_t)snprintf(out + p, cap - p, "]}");
    free(list);
    int r = http_respond_json(res, 200, out);
    free(out);
    return r;
}

int ctrl_tasks_create(const HttpRequest *req, HttpResponse *res)
{
    AuthSession s;
    int rc = require_session(req, res, &s);
    if (rc != SUCCESS) return rc;
    if (s.type != USER_TYPE_COORD && s.type != USER_TYPE_PROF)
        return http_respond_error(res, 403, "forbidden");

    long long class_id = extract_int(req->body, "class_id", -1);
    if (class_id <= 0) return http_respond_error(res, 400, "class_id required");

    char subject[64]={0}, title[121]={0}, desc[1024]={0}, due[16]={0};
    if (!extract_str(req->body, "subject", subject, sizeof subject))
        return http_respond_error(res, 400, "subject required");
    if (!extract_str(req->body, "title", title, sizeof title))
        return http_respond_error(res, 400, "title required");
    extract_str(req->body, "description", desc, sizeof desc);
    if (!extract_str(req->body, "due_date", due, sizeof due))
        return http_respond_error(res, 400, "due_date required");
    if (!validation_is_valid_date(due))
        return http_respond_error(res, 400, "invalid due_date");

    /* Para professores, o "teacher_id" implícito é o user_id.
     * No banco, "teachers.id" precisa ser resolvido. Aqui simplificamos
     * aceitando teacher_id no body OU usando o user_id do autor. */
    long long teacher_id = extract_int(req->body, "teacher_id", 0);
    if (s.type == USER_TYPE_PROF) {
        if (teacher_id_for_session(&s, &teacher_id) != SUCCESS)
            return http_respond_error(res, 403, "teacher profile not found");
    } else if (teacher_id <= 0) {
        return http_respond_error(res, 400, "teacher_id required");
    }

    Task t;
    rc = tasks_create(teacher_id, class_id, subject, title, desc, due, &t);
    if (rc == ERROR_FORBIDDEN) return http_respond_error(res, 403, "not linked to class");
    if (rc != SUCCESS)         return http_respond_error(res, 400, "create failed");

    char out[512];
    snprintf(out, sizeof out,
        "{\"id\":%lld,\"class_id\":%lld,\"subject\":\"%s\",\"title\":\"%s\","
        "\"due_date\":\"%s\"}",
        (long long)t.id, (long long)t.class_id, t.subject, t.title, t.due_date);
    return http_respond_json(res, 201, out);
}

int ctrl_tasks_update(const HttpRequest *req, HttpResponse *res, const char *id_str)
{
    AuthSession s;
    int rc = require_session(req, res, &s);
    if (rc != SUCCESS) return rc;
    if (s.type != USER_TYPE_COORD && s.type != USER_TYPE_PROF)
        return http_respond_error(res, 403, "forbidden");

    long long task_id = strtoll(id_str, NULL, 10);
    if (task_id <= 0) return http_respond_error(res, 400, "invalid id");

    char subject[64]={0}, title[121]={0}, desc[1024]={0}, due[16]={0};
    if (!extract_str(req->body, "subject", subject, sizeof subject) ||
        !extract_str(req->body, "title",   title,   sizeof title)   ||
        !extract_str(req->body, "due_date",due,     sizeof due))
        return http_respond_error(res, 400, "missing fields");
    extract_str(req->body, "description", desc, sizeof desc);
    if (!validation_is_valid_date(due))
        return http_respond_error(res, 400, "invalid due_date");

    bool is_coord = (s.type == USER_TYPE_COORD);
    long long acting_teacher_id = 0;
    if (!is_coord && teacher_id_for_session(&s, &acting_teacher_id) != SUCCESS)
        return http_respond_error(res, 403, "teacher profile not found");
    rc = tasks_update(task_id, acting_teacher_id, is_coord, subject, title, desc, due);
    if (rc == ERROR_FORBIDDEN) return http_respond_error(res, 403, "not owner");
    if (rc == ERROR_NOT_FOUND) return http_respond_error(res, 404, "not found");
    if (rc != SUCCESS)          return http_respond_error(res, 400, "update failed");
    return http_respond_json(res, 200, "{\"ok\":true}");
}

int ctrl_tasks_delete(const HttpRequest *req, HttpResponse *res, const char *id_str)
{
    AuthSession s;
    int rc = require_session(req, res, &s);
    if (rc != SUCCESS) return rc;
    if (s.type != USER_TYPE_COORD && s.type != USER_TYPE_PROF)
        return http_respond_error(res, 403, "forbidden");

    long long task_id = strtoll(id_str, NULL, 10);
    if (task_id <= 0) return http_respond_error(res, 400, "invalid id");
    bool is_coord = (s.type == USER_TYPE_COORD);
    long long acting_teacher_id = 0;
    if (!is_coord && teacher_id_for_session(&s, &acting_teacher_id) != SUCCESS)
        return http_respond_error(res, 403, "teacher profile not found");
    rc = tasks_delete(task_id, acting_teacher_id, is_coord);
    if (rc == ERROR_FORBIDDEN) return http_respond_error(res, 403, "not owner");
    if (rc == ERROR_NOT_FOUND) return http_respond_error(res, 404, "not found");
    if (rc != SUCCESS)          return http_respond_error(res, 400, "delete failed");
    return http_respond_json(res, 200, "{\"ok\":true}");
}
