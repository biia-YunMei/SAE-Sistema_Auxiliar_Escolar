#include "http_server.h"
#include "auth.h"
#include "database.h"
#include "controllers.h"
#include "validation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void json_escape(const char *value, char *out, size_t cap)
{
    size_t used = 0;
    static const char hex[] = "0123456789abcdef";
    for (const unsigned char *p = (const unsigned char *)(value ? value : ""); *p; ++p) {
        if (*p == '"' || *p == '\\') {
            if (used + 2 >= cap) break;
            out[used++] = '\\'; out[used++] = (char)*p;
        } else if (*p == '\n' || *p == '\r' || *p == '\t') {
            if (used + 2 >= cap) break;
            out[used++] = '\\'; out[used++] = *p == '\n' ? 'n' : (*p == '\r' ? 'r' : 't');
        } else if (*p < 0x20) {
            if (used + 6 >= cap) break;
            out[used++] = '\\'; out[used++] = 'u'; out[used++] = '0'; out[used++] = '0';
            out[used++] = hex[*p >> 4]; out[used++] = hex[*p & 0x0f];
        } else {
            if (used + 1 >= cap) break;
            out[used++] = (char)*p;
        }
    }
    out[used] = '\0';
}

static int text_field(const char *body, const char *key, char *out, size_t cap)
{
    char find[64]; int n = snprintf(find, sizeof find, "\"%s\":\"", key);
    if (!body || n < 0 || (size_t)n >= sizeof find) return 0;
    const char *p = strstr(body, find); if (!p) return 0; p += n;
    size_t used = 0;
    while (*p && *p != '"') {
        char ch = *p++;
        if (ch == '\\') {
            if (!*p) return 0;
            ch = *p++;
            if (ch == 'n') ch = '\n'; else if (ch == 'r') ch = '\r'; else if (ch == 't') ch = '\t';
        }
        if (used + 1 >= cap) return 0;
        out[used++] = ch;
    }
    if (*p != '"') return 0;
    out[used] = '\0'; return 1;
}

static long int_field(const char *body, const char *key)
{
    char find[64]; int n = snprintf(find, sizeof find, "\"%s\":", key);
    const char *p = body ? strstr(body, find) : NULL;
    return p ? strtol(p + n, NULL, 10) : 0;
}

static int coordinator(const HttpRequest *req)
{
    AuthSession session;
    return req->auth && auth_session_resolve(req->auth, &session) == SUCCESS &&
           session.type == USER_TYPE_COORD ? SUCCESS : ERROR_FORBIDDEN;
}

int ctrl_notices_list(const HttpRequest *req, HttpResponse *res)
{
    if (!req->auth) return http_respond_error(res, 401, "missing authorization");
    AuthSession session;
    if (auth_session_resolve(req->auth, &session) != SUCCESS)
        return http_respond_error(res, 401, "invalid session");
    int student_view = session.type == USER_TYPE_STUD;
    if (session.type != USER_TYPE_COORD && session.type != USER_TYPE_PROF && !student_view)
        return http_respond_error(res, 403, "forbidden");
    long long student_class_id = 0;
    if (student_view) {
        sqlite3_stmt *student = NULL;
        if (db_prepare("SELECT class_id FROM students WHERE user_id=?;", &student) != SUCCESS)
            return http_respond_error(res, 500, "database error");
        db_bind_int64(student, 1, session.user_id);
        int found = sqlite3_step(student) == SQLITE_ROW;
        if (found) student_class_id = sqlite3_column_int64(student, 0);
        sqlite3_finalize(student);
        if (!found || student_class_id <= 0)
            return http_respond_error(res, 403, "student has no class");
    }
    sqlite3_stmt *st = NULL;
    const char *sql = student_view
        ? "SELECT n.id,n.class_id,n.title,n.content,n.created_at,c.name "
          "FROM notices n LEFT JOIN classes c ON c.id=n.class_id "
          "WHERE n.status='ACTIVE' AND (n.class_id IS NULL OR n.class_id=?) ORDER BY n.id DESC LIMIT 20;"
        : "SELECT n.id,n.class_id,n.title,n.content,n.created_at,c.name "
                   "FROM notices n LEFT JOIN classes c ON c.id=n.class_id "
                   "WHERE n.status='ACTIVE' ORDER BY n.id DESC LIMIT 20;";
    if (db_prepare(sql, &st) != SUCCESS)
        return http_respond_error(res, 500, "database error");
    if (student_view) db_bind_int64(st, 1, student_class_id);
    size_t json_cap = 270000;
    char *json = malloc(json_cap);
    if (!json) { sqlite3_finalize(st); return http_respond_error(res, 500, "oom"); }
    memcpy(json, "{\"items\":[", 10); size_t used = 10; int first = 1;
    while (sqlite3_step(st) == SQLITE_ROW && used + 13000 < json_cap) {
        char title[256], content[12290], created[64], class_name[256];
        json_escape((const char *)sqlite3_column_text(st, 2), title, sizeof title);
        json_escape((const char *)sqlite3_column_text(st, 3), content, sizeof content);
        json_escape((const char *)sqlite3_column_text(st, 4), created, sizeof created);
        json_escape(sqlite3_column_text(st, 5) ? (const char *)sqlite3_column_text(st, 5) : "Todas", class_name, sizeof class_name);
        int n = snprintf(json + used, json_cap - used,
            "%s{\"id\":%lld,\"class_id\":%lld,\"title\":\"%s\",\"content\":\"%s\",\"created_at\":\"%s\",\"class_name\":\"%s\"}",
            first ? "" : ",", (long long)sqlite3_column_int64(st, 0),
            (long long)sqlite3_column_int64(st, 1), title, content, created, class_name);
        if (n < 0) break;
        used += (size_t)n;
        first = 0;
    }
    sqlite3_finalize(st); snprintf(json + used, json_cap - used, "]}");
    int response = http_respond_json(res, 200, json);
    free(json);
    return response;
}

static int valid_notice_class(long class_id)
{
    if (class_id <= 0) return class_id == 0;
    sqlite3_stmt *statement = NULL;
    if (db_prepare("SELECT status FROM classes WHERE id=?;", &statement) != SUCCESS)
        return -1;
    db_bind_int64(statement, 1, class_id);
    int active = sqlite3_step(statement) == SQLITE_ROW &&
        strcmp((const char *)sqlite3_column_text(statement, 0), "ACTIVE") == 0;
    sqlite3_finalize(statement);
    return active;
}

int ctrl_notices_create(const HttpRequest *req, HttpResponse *res)
{
    if (coordinator(req) != SUCCESS) return http_respond_error(res, 403, "coordinator only");
    char title[121] = {0}, content[2048] = {0}; long class_id = int_field(req->body, "class_id");
    if (!text_field(req->body, "title", title, sizeof title) || !text_field(req->body, "content", content, sizeof content))
        return http_respond_error(res, 400, "title and content are required");
    if (!validation_length_between(title, 1, 120) ||
        !validation_length_between(content, 1, 2000) ||
        !validation_is_safe_text(title) || !validation_is_safe_text(content))
        return http_respond_error(res, 400, "invalid notice content");
    int class_valid = valid_notice_class(class_id);
    if (class_valid < 0) return http_respond_error(res, 500, "database error");
    if (!class_valid) return http_respond_error(res, 404, "class not found");
    AuthSession session; auth_session_resolve(req->auth, &session);
    sqlite3_stmt *st = NULL;
    if (db_prepare("INSERT INTO notices(author_id,class_id,title,content) VALUES(?,?,?,?);", &st) != SUCCESS)
        return http_respond_error(res, 500, "database error");
    db_bind_int64(st, 1, (long long)session.user_id);
    if (class_id > 0) db_bind_int64(st, 2, class_id); else db_bind_null(st, 2);
    db_bind_text(st, 3, title); db_bind_text(st, 4, content);
    int ok = sqlite3_step(st) == SQLITE_DONE;
    long long id = ok ? (long long)sqlite3_last_insert_rowid(database_get()) : 0;
    sqlite3_finalize(st);
    if (!ok) return http_respond_error(res, 400, "could not create notice");
    char response[80]; snprintf(response, sizeof response, "{\"ok\":true,\"id\":%lld}", id);
    return http_respond_json(res, 201, response);
}

int ctrl_notices_update(const HttpRequest *req, HttpResponse *res, const char *id)
{
    if (coordinator(req) != SUCCESS) return http_respond_error(res, 403, "coordinator only");
    char *end = NULL; long notice_id = strtol(id, &end, 10);
    if (!end || *end || notice_id <= 0) return http_respond_error(res, 400, "invalid id");
    char title[121] = {0}, content[2048] = {0};
    long class_id = int_field(req->body, "class_id");
    if (!text_field(req->body, "title", title, sizeof title) ||
        !text_field(req->body, "content", content, sizeof content))
        return http_respond_error(res, 400, "title and content are required");
    if (!validation_length_between(title, 1, 120) ||
        !validation_length_between(content, 1, 2000) ||
        !validation_is_safe_text(title) || !validation_is_safe_text(content))
        return http_respond_error(res, 400, "invalid notice content");
    int class_valid = valid_notice_class(class_id);
    if (class_valid < 0) return http_respond_error(res, 500, "database error");
    if (!class_valid) return http_respond_error(res, 404, "class not found");
    sqlite3_stmt *st = NULL;
    if (db_prepare("UPDATE notices SET class_id=?,title=?,content=? WHERE id=?;", &st) != SUCCESS)
        return http_respond_error(res, 500, "database error");
    if (class_id > 0) db_bind_int64(st, 1, class_id); else db_bind_null(st, 1);
    db_bind_text(st, 2, title); db_bind_text(st, 3, content); db_bind_int64(st, 4, notice_id);
    int done = sqlite3_step(st) == SQLITE_DONE; sqlite3_finalize(st);
    if (!done) return http_respond_error(res, 400, "update failed");
    if (!sqlite3_changes(database_get())) return http_respond_error(res, 404, "not found");
    return http_respond_json(res, 200, "{\"ok\":true}");
}

int ctrl_notices_delete(const HttpRequest *req, HttpResponse *res, const char *id)
{
    if (coordinator(req) != SUCCESS) return http_respond_error(res, 403, "coordinator only");
    char *end = NULL; long notice_id = strtol(id, &end, 10);
    if (!end || *end || notice_id <= 0) return http_respond_error(res, 400, "invalid id");
    sqlite3_stmt *st = NULL;
    if (db_prepare("DELETE FROM notices WHERE id=?;", &st) != SUCCESS)
        return http_respond_error(res, 500, "database error");
    db_bind_int64(st, 1, notice_id);
    int done = sqlite3_step(st) == SQLITE_DONE; sqlite3_finalize(st);
    if (!done) return http_respond_error(res, 400, "delete failed");
    if (!sqlite3_changes(database_get())) return http_respond_error(res, 404, "not found");
    return http_respond_json(res, 200, "{\"ok\":true}");
}
