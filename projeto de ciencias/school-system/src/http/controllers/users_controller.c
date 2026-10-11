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
#include "json_util.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

static void users_json_escape(const char *value, char *out, size_t cap)
{
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)(value ? value : ""); *p; ++p) {
        if (*p < 0x20) continue;
        if (*p == '"' || *p == '\\') {
            if (used + 2 >= cap) break;
            out[used++] = '\\';
        }
        if (used + 1 >= cap) break;
        out[used++] = (char)*p;
    }
    if (cap) out[used] = '\0';
}

static int extract_str(const char *b, const char *k, char *o, size_t c)
{
    return json_object_get_string(b, k, o, c);
}

static int require_coord(const HttpRequest *req, HttpResponse *res, AuthSession *out)
{
    if (!req->auth) {
        (void)http_respond_error(res, 401, "missing authorization");
        return ERROR_UNAUTHORIZED;
    }
    if (auth_session_resolve(req->auth, out) != SUCCESS) {
        (void)http_respond_error(res, 401, "invalid session");
        return ERROR_UNAUTHORIZED;
    }
    if (out->type != USER_TYPE_COORD) {
        (void)http_respond_error(res, 403, "coordinator only");
        return ERROR_FORBIDDEN;
    }
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
    if (!extract_str(req->body, "password", pass, sizeof pass) ||
        !validation_length_between(pass, 8, 127))
        return http_respond_error(res, 400, "invalid password length");
    if (!validation_is_valid_phone(phone)) return http_respond_error(res, 400, "invalid phone");

    UserType t = user_type_from(type_s);
    if (t == USER_TYPE_INVALID) return http_respond_error(res, 400, "invalid type");
    if (t == USER_TYPE_STUD)
        return http_respond_error(res, 400, "use student registration");

    User u;
    rc = users_create(name, phone, t, pass, &u);
    if (rc == ERROR_ALREADY_EXISTS) return http_respond_error(res, 409, "phone already registered");
    if (rc != SUCCESS)              return http_respond_error(res, 400, "create failed");

    char out[512], safe_name[256];
    users_json_escape(u.name, safe_name, sizeof safe_name);
    snprintf(out, sizeof out,
        "{\"id\":%lld,\"name\":\"%s\",\"phone\":\"%s\",\"type\":\"%s\"}",
        (long long)u.id, safe_name, u.phone, user_type_str(u.type));
    return http_respond_json(res, 201, out);
}

int ctrl_users_list(const HttpRequest *req, HttpResponse *res)
{
    AuthSession s;
    if (!req->auth || auth_session_resolve(req->auth, &s) != SUCCESS)
        return http_respond_error(res, 401, "invalid session");
    if (s.type != USER_TYPE_COORD)
        return http_respond_error(res, 403, "forbidden");

    sqlite3_stmt *st = NULL;
    if (db_prepare("SELECT id,name,phone,user_type,status FROM users;", &st) != SUCCESS)
        return http_respond_error(res, 500, "query failed");

    size_t cap = 256;
    char *out = (char *)malloc(cap);
    if (!out) { sqlite3_finalize(st); return http_respond_error(res, 500, "oom"); }
    size_t p = 0;
    int count = 0;
    p += (size_t)snprintf(out + p, cap - p, "{\"items\":[");
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (p + 1200 > cap) {
            if (cap > SIZE_MAX / 2) { free(out); sqlite3_finalize(st); return http_respond_error(res, 500, "response too large"); }
            cap *= 2;
            char *q = (char *)realloc(out, cap);
            if (!q) { free(out); sqlite3_finalize(st); return http_respond_error(res, 500, "oom"); }
            out = q;
        }
        char safe_name[768];
        users_json_escape((const char *)sqlite3_column_text(st, 1), safe_name, sizeof safe_name);
        p += (size_t)snprintf(out + p, cap - p,
            "%s{\"id\":%lld,\"name\":\"%s\",\"phone\":\"%s\","
            "\"type\":\"%s\",\"status\":\"%s\"}",
            count ? "," : "",
            (long long)sqlite3_column_int64(st, 0),
            safe_name,
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

int ctrl_users_update(const HttpRequest *req, HttpResponse *res, const char *id_str)
{
    AuthSession session;
    int rc = require_coord(req, res, &session);
    if (rc != SUCCESS) return rc;
    int64_t parsed_id;
    if (!parse_int64_strict(id_str, &parsed_id) || parsed_id <= 0)
        return http_respond_error(res, 400, "invalid id");
    long long id = (long long)parsed_id;

    User current;
    if (users_find_by_id(id, &current) != SUCCESS)
        return http_respond_error(res, 404, "not found");
    char name[121] = {0}, phone[32] = {0}, password[128] = {0};
    if (!extract_str(req->body, "name", name, sizeof name) ||
        !extract_str(req->body, "phone", phone, sizeof phone))
        return http_respond_error(res, 400, "name and phone are required");
    (void)extract_str(req->body, "password", password, sizeof password);

    User updated;
    rc = users_update(id, name, phone, current.type,
                      password[0] ? password : NULL, &updated);
    if (rc == ERROR_ALREADY_EXISTS)
        return http_respond_error(res, 409, "phone already registered");
    if (rc != SUCCESS) return http_respond_error(res, 400, "update failed");
    return http_respond_json(res, 200, "{\"ok\":true,\"message\":\"user updated\"}");
}

int ctrl_users_delete(const HttpRequest *req, HttpResponse *res, const char *id_str)
{
    AuthSession session;
    int rc = require_coord(req, res, &session);
    if (rc != SUCCESS) return rc;
    int64_t parsed_id;
    if (!parse_int64_strict(id_str, &parsed_id) || parsed_id <= 0)
        return http_respond_error(res, 400, "invalid id");
    long long id = (long long)parsed_id;
    if (id == session.user_id)
        return http_respond_error(res, 400, "cannot delete own user");
    rc = users_delete(id);
    if (rc == ERROR_NOT_FOUND) return http_respond_error(res, 404, "not found");
    if (rc != SUCCESS) return http_respond_error(res, 400, "delete failed");
    return http_respond_json(res, 200, "{\"ok\":true,\"message\":\"user deleted\"}");
}
