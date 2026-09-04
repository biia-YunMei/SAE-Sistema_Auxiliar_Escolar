/* users_controller.c
 * Endpoints:
 *   POST /api/users        — cadastrar (somente COORD)
 *   GET  /api/users        — listar  (somente COORD)
 */
#include "http_server.h"
#include "auth.h"
#include "users.h"
#include "validation.h"
#include "database.h"
#include "logger.h"
#include "controllers.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int extract_str(const char *b, const char *k, char *o, size_t c)
{
    char n[64]; int x = snprintf(n, sizeof n, "\"%s\":\"", k);
    if (x <= 0 || (size_t)x >= sizeof n) return 0;
    const char *p = strstr(b, n);
    if (!p) return 0;
    p += x;
    const char *e = strchr(p, '"');
    if (!e) return 0;
    size_t l = (size_t)(e - p);
    if (l >= c) l = c - 1;
    memcpy(o, p, l); o[l] = '\0';
    return 1;
}

static int require_coord(const HttpRequest *req, HttpResponse *res, AuthSession *out)
{
    if (!req->auth) return http_respond_error(res, 401, "missing authorization");
    if (auth_session_resolve(req->auth, out) != SUCCESS)
        return http_respond_error(res, 401, "invalid session");
    if (out->type != USER_TYPE_COORD)
        return http_respond_error(res, 403, "coordinator only");
    return SUCCESS;
}

int ctrl_users_create(const HttpRequest *req, HttpResponse *res)
{
    AuthSession s;
    int rc = require_coord(req, res, &s);
    if (rc != SUCCESS) return rc;

    char name[121] = {0}, phone[32] = {0}, type_s[16] = {0}, pass[128] = {0};
    if (!extract_str(req->body, "name",  name,  sizeof name))  return http_respond_error(res, 400, "name required");
    if (!extract_str(req->body, "phone", phone, sizeof phone)) return http_respond_error(res, 400, "phone required");
    if (!extract_str(req->body, "type",  type_s, sizeof type_s)) return http_respond_error(res, 400, "type required");
    extract_str(req->body, "password", pass, sizeof pass);
    if (!validation_is_valid_phone(phone)) return http_respond_error(res, 400, "invalid phone");

    UserType t = user_type_from(type_s);
    if (t == USER_TYPE_INVALID) return http_respond_error(res, 400, "invalid type");

    User u;
    rc = users_create(name, phone, t, pass[0] ? pass : NULL, &u);
    if (rc == ERROR_ALREADY_EXISTS) return http_respond_error(res, 409, "phone already registered");
    if (rc != SUCCESS)              return http_respond_error(res, 400, "create failed");

    /* Se for professor, cria o registro em teachers. */
    if (t == USER_TYPE_PROF) {
        sqlite3_stmt *st = NULL;
        db_prepare("INSERT INTO teachers(user_id) VALUES(?);", &st);
        db_bind_int64(st, 1, (long long)u.id);
        sqlite3_step(st);
        sqlite3_finalize(st);
    } else if (t == USER_TYPE_STUD) {
        sqlite3_stmt *st = NULL;
        db_prepare("INSERT INTO students(user_id) VALUES(?);", &st);
        db_bind_int64(st, 1, (long long)u.id);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }

    char out[256];
    snprintf(out, sizeof out,
        "{\"id\":%lld,\"name\":\"%s\",\"phone\":\"%s\",\"type\":\"%s\"}",
        (long long)u.id, u.name, u.phone, user_type_str(u.type));
    return http_respond_json(res, 201, out);
}

int ctrl_users_list(const HttpRequest *req, HttpResponse *res)
{
    AuthSession s;
    int rc = require_coord(req, res, &s);
    if (rc != SUCCESS) return rc;

    sqlite3_stmt *st = NULL;
    if (db_prepare("SELECT id,name,phone,user_type,status FROM users;", &st) != SUCCESS)
        return http_respond_error(res, 500, "query failed");

    size_t cap = 256;
    char *out = (char *)malloc(cap);
    size_t p = 0;
    int count = 0;
    p += (size_t)snprintf(out + p, cap - p, "{\"items\":[");
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (p + 512 > cap) {
            cap *= 2;
            char *q = (char *)realloc(out, cap);
            if (!q) { free(out); sqlite3_finalize(st); return http_respond_error(res, 500, "oom"); }
            out = q;
        }
        p += (size_t)snprintf(out + p, cap - p,
            "%s{\"id\":%lld,\"name\":\"%s\",\"phone\":\"%s\","
            "\"type\":\"%s\",\"status\":\"%s\"}",
            count ? "," : "",
            (long long)sqlite3_column_int64(st, 0),
            (const char *)sqlite3_column_text(st, 1),
            (const char *)sqlite3_column_text(st, 2),
            (const char *)sqlite3_column_text(st, 3),
            (const char *)sqlite3_column_text(st, 4));
        count++;
    }
    sqlite3_finalize(st);
    p += (size_t)snprintf(out + p, cap - p, "],\"count\":%d}", count);
    int r = http_respond_json(res, 200, out);
    free(out);
    return r;
}
