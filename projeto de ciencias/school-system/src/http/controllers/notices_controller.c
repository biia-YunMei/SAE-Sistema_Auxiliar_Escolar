#include "http_server.h"
#include "auth.h"
#include "database.h"
#include "controllers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int text_field(const char *body, const char *key, char *out, size_t cap)
{
    char find[64]; int n = snprintf(find, sizeof find, "\"%s\":\"", key);
    if (!body || n < 0 || (size_t)n >= sizeof find) return 0;
    const char *p = strstr(body, find); if (!p) return 0; p += n;
    const char *e = strchr(p, '"'); if (!e || (size_t)(e - p) >= cap) return 0;
    memcpy(out, p, (size_t)(e - p)); out[e - p] = '\0'; return 1;
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
    sqlite3_stmt *st = NULL;
    if (db_prepare("SELECT n.id,n.class_id,n.title,n.content,n.created_at,c.name "
                   "FROM notices n LEFT JOIN classes c ON c.id=n.class_id "
                   "WHERE n.status='ACTIVE' ORDER BY n.id DESC;", &st) != SUCCESS)
        return http_respond_error(res, 500, "database error");
    char json[60000] = "{\"items\":["; size_t used = strlen(json); int first = 1;
    while (sqlite3_step(st) == SQLITE_ROW && used + 1024 < sizeof json) {
        int n = snprintf(json + used, sizeof json - used,
            "%s{\"id\":%lld,\"class_id\":%lld,\"title\":\"%s\",\"content\":\"%s\",\"created_at\":\"%s\",\"class_name\":\"%s\"}",
            first ? "" : ",", (long long)sqlite3_column_int64(st, 0),
            (long long)sqlite3_column_int64(st, 1), (const char *)sqlite3_column_text(st, 2),
            (const char *)sqlite3_column_text(st, 3), (const char *)sqlite3_column_text(st, 4),
            sqlite3_column_text(st, 5) ? (const char *)sqlite3_column_text(st, 5) : "Todas");
        if (n < 0) break;
        used += (size_t)n;
        first = 0;
    }
    sqlite3_finalize(st); snprintf(json + used, sizeof json - used, "]}");
    return http_respond_json(res, 200, json);
}

int ctrl_notices_create(const HttpRequest *req, HttpResponse *res)
{
    if (coordinator(req) != SUCCESS) return http_respond_error(res, 403, "coordinator only");
    char title[121] = {0}, content[2048] = {0}; long class_id = int_field(req->body, "class_id");
    if (!text_field(req->body, "title", title, sizeof title) || !text_field(req->body, "content", content, sizeof content))
        return http_respond_error(res, 400, "title and content are required");
    AuthSession session; auth_session_resolve(req->auth, &session);
    sqlite3_stmt *st = NULL;
    if (db_prepare("INSERT INTO notices(author_id,class_id,title,content) VALUES(?,?,?,?);", &st) != SUCCESS)
        return http_respond_error(res, 500, "database error");
    db_bind_int64(st, 1, (long long)session.user_id);
    if (class_id > 0) db_bind_int64(st, 2, class_id); else db_bind_null(st, 2);
    db_bind_text(st, 3, title); db_bind_text(st, 4, content);
    int ok = sqlite3_step(st) == SQLITE_DONE; sqlite3_finalize(st);
    return ok ? http_respond_json(res, 201, "{\"ok\":true}") : http_respond_error(res, 400, "could not create notice");
}
