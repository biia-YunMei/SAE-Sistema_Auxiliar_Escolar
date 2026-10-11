#include "http_server.h"
#include "auth.h"
#include "database.h"
#include "controllers.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void message_json(const char *value, char *out, size_t cap)
{
    static const char hex[] = "0123456789abcdef";
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)(value ? value : "");
         *p && used + 1 < cap; ++p) {
        if (*p == '"' || *p == '\\') {
            if (used + 2 >= cap) break;
            out[used++] = '\\';
            out[used++] = (char)*p;
        } else if (*p < 0x20) {
            if (used + 6 >= cap) break;
            out[used++] = '\\'; out[used++] = 'u';
            out[used++] = '0'; out[used++] = '0';
            out[used++] = hex[*p >> 4]; out[used++] = hex[*p & 0x0f];
        } else {
            out[used++] = (char)*p;
        }
    }
    if (cap) out[used] = '\0';
}

int ctrl_messages_list(const HttpRequest *req, HttpResponse *res)
{
    AuthSession session;
    if (!req->auth || auth_session_resolve(req->auth, &session) != SUCCESS)
        return http_respond_error(res, 401, "invalid session");
    sqlite3_stmt *statement = NULL;
    const char *sql = session.type == USER_TYPE_COORD
        ? "SELECT m.sender_phone,m.content,m.msg_type,m.status,m.created_at "
          "FROM messages m WHERE m.user_type='ANONYMOUS' ORDER BY m.id DESC LIMIT 10;"
        : session.type == USER_TYPE_PROF
        ? "SELECT m.sender_phone,m.content,m.msg_type,m.status,m.created_at "
          "FROM messages m JOIN anonymous_students a ON a.phone=m.sender_phone "
          "JOIN teacher_classes tc ON tc.class_id=a.class_id "
          "JOIN teachers t ON t.id=tc.teacher_id "
          "WHERE m.user_type='ANONYMOUS' AND t.user_id=? ORDER BY m.id DESC LIMIT 10;"
        : NULL;
    if (!sql)
        return http_respond_error(res, 403, "forbidden");
    if (db_prepare(sql, &statement) != SUCCESS)
        return http_respond_error(res, 500, "database error");
    if (session.type == USER_TYPE_PROF) db_bind_int64(statement, 1, (long long)session.user_id);

    size_t capacity = 4096, used = 0;
    char *json = malloc(capacity);
    if (!json) {
        sqlite3_finalize(statement);
        return http_respond_error(res, 500, "out of memory");
    }
    used += (size_t)snprintf(json + used, capacity - used, "{\"items\":[");
    size_t count = 0;
    int step;
    while ((step = sqlite3_step(statement)) == SQLITE_ROW) {
        char sender[192], content[24578], kind[32], status[32], created[96];
        message_json((const char *)sqlite3_column_text(statement, 0), sender, sizeof sender);
        message_json((const char *)sqlite3_column_text(statement, 1), content, sizeof content);
        message_json((const char *)sqlite3_column_text(statement, 2), kind, sizeof kind);
        message_json((const char *)sqlite3_column_text(statement, 3), status, sizeof status);
        message_json((const char *)sqlite3_column_text(statement, 4), created, sizeof created);
        size_t needed = strlen(sender) + strlen(content) + strlen(kind) + strlen(status) + strlen(created) + 160;
        if (used + needed + 32 > capacity) {
            size_t next = capacity;
            while (next < used + needed + 32 && next <= SIZE_MAX / 2) next *= 2;
            if (next < used + needed + 32) {
                free(json); sqlite3_finalize(statement);
                return http_respond_error(res, 500, "response too large");
            }
            char *grown = realloc(json, next);
            if (!grown) {
                free(json); sqlite3_finalize(statement);
                return http_respond_error(res, 500, "out of memory");
            }
            json = grown; capacity = next;
        }
        int n = snprintf(json + used, capacity - used,
            "%s{\"sender_ref\":\"%s\",\"content\":\"%s\",\"kind\":\"%s\",\"status\":\"%s\",\"created_at\":\"%s\"}",
            count ? "," : "", sender, content, kind, status, created);
        if (n < 0 || (size_t)n >= capacity - used) {
            free(json); sqlite3_finalize(statement);
            return http_respond_error(res, 500, "response too large");
        }
        used += (size_t)n;
        ++count;
    }
    sqlite3_finalize(statement);
    if (step != SQLITE_DONE) {
        free(json);
        return http_respond_error(res, 500, "database error");
    }
    int n = snprintf(json + used, capacity - used, "],\"count\":%zu}", count);
    if (n < 0 || (size_t)n >= capacity - used) {
        free(json);
        return http_respond_error(res, 500, "response too large");
    }
    int result = http_respond_json(res, 200, json);
    free(json);
    return result;
}
