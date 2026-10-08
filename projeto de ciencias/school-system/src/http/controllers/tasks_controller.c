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
#include "classes.h"
#include "users.h"
#include "database.h"
#include "validation.h"
#include "logger.h"
#include "controllers.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

static void json_escape(const char *value, char *out, size_t cap)
{
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)(value ? value : ""); *p && used + 1 < cap; ++p) {
        if (*p == '"' || *p == '\\') {
            if (used + 2 >= cap) break;
            out[used++] = '\\'; out[used++] = (char)*p;
        } else if (*p >= 0x20) out[used++] = (char)*p;
    }
    out[used] = '\0';
}

/* extrai "key":"value" ou "key":number do body JSON ingênuo */
static int extract_str(const char *body, const char *key, char *out, size_t cap)
{
    char needle[64]; int n = snprintf(needle, sizeof needle, "\"%s\":\"", key);
    if (n < 0 || (size_t)n >= sizeof needle) return 0;
    const char *p = strstr(body, needle);
    if (!p) return 0;
    p += n;
    size_t used = 0;
    while (*p && *p != '"') {
        char ch = *p++;
        if (ch == '\\') {
            if (!*p) return 0;
            ch = *p++;
            if (ch == 'n') ch = '\n';
            else if (ch == 'r') ch = '\r';
            else if (ch == 't') ch = '\t';
        }
        if (used + 1 >= cap) return 0;
        out[used++] = ch;
    }
    if (*p != '"') return 0;
    out[used] = '\0';
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
    if (!req->auth) {
        (void)http_respond_error(res, 401, "missing authorization");
        return ERROR_UNAUTHORIZED;
    }
    int rc = auth_session_resolve(req->auth, s);
    if (rc != SUCCESS) {
        (void)http_respond_error(res, 401, "invalid session");
        return ERROR_UNAUTHORIZED;
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

static int find_authorized_class(const char *name, long long teacher_id, long long *class_id)
{
    Class classroom;
    if (!name || !validation_length_between(name, 1, 63)) return ERROR_INVALID_INPUT;
    int rc = classes_find_by_name(name, &classroom);
    if (rc != SUCCESS || strcmp(classroom.status, "ACTIVE") != 0) return ERROR_NOT_FOUND;
    *class_id = (long long)classroom.id;
    if (!classes_teacher_has_access(teacher_id, *class_id)) return ERROR_FORBIDDEN;
    return SUCCESS;
}

static int student_class_for_session(const AuthSession *s, long long *out)
{
    sqlite3_stmt *st = NULL;
    if (!s || !out || s->type != USER_TYPE_STUD ||
        db_prepare("SELECT class_id FROM students WHERE user_id=?;", &st) != SUCCESS)
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
    int mine = get_query_param(req->query, "mine", qbuf, sizeof qbuf) && strcmp(qbuf, "1") == 0;
    int all = get_query_param(req->query, "all", qbuf, sizeof qbuf) && strcmp(qbuf, "1") == 0;
    char exact_date[16] = {0}, date_from[16] = {0}, date_to[16] = {0};
    int has_exact = get_query_param(req->query, "due_date", exact_date, sizeof exact_date);
    int has_from = get_query_param(req->query, "date_from", date_from, sizeof date_from);
    int has_to = get_query_param(req->query, "date_to", date_to, sizeof date_to);
    if ((has_exact && !validation_is_valid_date(exact_date)) ||
        (has_from && !validation_is_valid_date(date_from)) ||
        (has_to && !validation_is_valid_date(date_to)) ||
        (has_from && has_to && strcmp(date_from, date_to) > 0))
        return http_respond_error(res, 400, "invalid date filter");
    long long class_id = 0;
    if (all && s.type != USER_TYPE_COORD)
        return http_respond_error(res, 403, "coordinator only");
    if (!mine && !all) {
        if (!get_query_param(req->query, "class_id", qbuf, sizeof qbuf))
            return http_respond_error(res, 400, "class_id required");
        class_id = strtoll(qbuf, NULL, 10);
        if (class_id <= 0) return http_respond_error(res, 400, "class_id required");
    } else if (s.type != USER_TYPE_PROF) {
        return http_respond_error(res, 403, "forbidden");
    }

    if (s.type == USER_TYPE_STUD) {
        long long student_class_id = 0;
        if (student_class_for_session(&s, &student_class_id) != SUCCESS ||
            student_class_id != class_id)
            return http_respond_error(res, 403, "student tasks only");
    } else if (s.type == USER_TYPE_PROF && !mine) {
        long long teacher_id = 0;
        if (teacher_id_for_session(&s, &teacher_id) != SUCCESS ||
            !classes_teacher_has_access(teacher_id, class_id))
            return http_respond_error(res, 403, "not linked to class");
    }

    Task *list = NULL; size_t n = 0;
    if (all) rc = tasks_list_all(false, &list, &n);
    else if (mine) {
        long long teacher_id = 0;
        if (teacher_id_for_session(&s, &teacher_id) != SUCCESS)
            return http_respond_error(res, 403, "teacher profile not found");
        rc = tasks_list_by_teacher((int64_t)teacher_id, false, &list, &n);
    } else rc = tasks_list_by_class((int64_t)class_id, false, &list, &n);
    if (rc != SUCCESS) { return http_respond_error(res, 500, "list failed"); }

    if (has_exact || has_from || has_to) {
        size_t kept = 0;
        for (size_t i = 0; i < n; ++i) {
            const char *due = list[i].due_date;
            if ((has_exact && strcmp(due, exact_date) != 0) ||
                (has_from && strcmp(due, date_from) < 0) ||
                (has_to && strcmp(due, date_to) > 0)) continue;
            if (kept != i) list[kept] = list[i];
            ++kept;
        }
        n = kept;
    }

    /* Renderiza JSON manualmente. */
    if (n > (SIZE_MAX - 256) / 8192) { free(list); return http_respond_error(res, 500, "response too large"); }
    size_t cap = 256 + n * 8192;
    char *out = (char *)malloc(cap);
    if (!out) { free(list); return http_respond_error(res, 500, "oom"); }
    size_t p = 0;
    p += (size_t)snprintf(out + p, cap - p, "{\"count\":%zu,\"items\":[", n);
    for (size_t i = 0; i < n; i++) {
        char subject[128], title[256], description[2048];
        json_escape(list[i].subject, subject, sizeof subject);
        json_escape(list[i].title, title, sizeof title);
        json_escape(list[i].description, description, sizeof description);
        p += (size_t)snprintf(out + p, cap - p,
            "%s{\"id\":%lld,\"teacher_id\":%lld,\"class_id\":%lld,"
            "\"subject\":\"%s\",\"title\":\"%s\",\"description\":\"%s\","
            "\"due_date\":\"%s\",\"status\":\"%s\",\"created_at\":\"%s\"}",
            i ? "," : "",
            (long long)list[i].id, (long long)list[i].teacher_id,
            (long long)list[i].class_id,
            subject, title, description,
            list[i].due_date, task_status_str(list[i].status), list[i].created_at);
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
    char class_name[64] = {0};
    (void)extract_str(req->body, "class_name", class_name, sizeof class_name);
    if (class_id <= 0 && !class_name[0]) return http_respond_error(res, 400, "class_id required");

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
        if (class_id <= 0) {
            rc = find_authorized_class(class_name, teacher_id, &class_id);
            if (rc == ERROR_FORBIDDEN) return http_respond_error(res, 403, "not linked to class");
            if (rc != SUCCESS) return http_respond_error(res, 404, "class not found");
        } else if (!classes_teacher_has_access(teacher_id, class_id)) {
            return http_respond_error(res, 403, "not linked to class");
        }
    } else if (teacher_id <= 0) {
        return http_respond_error(res, 400, "teacher_id required");
    }

    Task t;
    rc = tasks_create(teacher_id, class_id, subject, title, desc, due, &t);
    if (rc == ERROR_FORBIDDEN) return http_respond_error(res, 403, "not linked to class");
    if (rc != SUCCESS)         return http_respond_error(res, 400, "create failed");

    char out[512], safe_subject[128], safe_title[256];
    json_escape(t.subject, safe_subject, sizeof safe_subject);
    json_escape(t.title, safe_title, sizeof safe_title);
    snprintf(out, sizeof out,
        "{\"id\":%lld,\"class_id\":%lld,\"subject\":\"%s\",\"title\":\"%s\","
        "\"due_date\":\"%s\"}",
        (long long)t.id, (long long)t.class_id, safe_subject, safe_title, t.due_date);
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
    return http_respond_json(res, 200, "{\"ok\":true,\"message\":\"task updated\"}");
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
    return http_respond_json(res, 200, "{\"ok\":true,\"message\":\"task deleted\"}");
}
