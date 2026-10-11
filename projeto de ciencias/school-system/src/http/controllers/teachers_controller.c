#include "http_server.h"
#include "auth.h"
#include "users.h"
#include "validation.h"
#include "database.h"
#include "controllers.h"
#include "json_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int field(const char *body, const char *key, char *out, size_t cap)
{
    return json_object_get_string(body, key, out, cap);
}

static int coordinator(const HttpRequest *req, HttpResponse *res, AuthSession *s)
{
    if (!req->auth || auth_session_resolve(req->auth, s) != SUCCESS) {
        (void)http_respond_error(res, 401, "invalid session");
        return ERROR_UNAUTHORIZED;
    }
    if (s->type != USER_TYPE_COORD) {
        (void)http_respond_error(res, 403, "coordinator only");
        return ERROR_FORBIDDEN;
    }
    return SUCCESS;
}

static void json_string(const char *value, char *out, size_t cap)
{
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)(value ? value : ""); *p; ++p) {
        if ((*p == '"' || *p == '\\') && used + 2 < cap) out[used++] = '\\';
        if (*p >= 0x20 && used + 1 < cap) out[used++] = (char)*p;
    }
    out[used] = '\0';
}

static int canonical_phone(const char *input, char *out, size_t cap)
{
    char digits[24]; size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)input; *p; ++p)
        if (*p >= '0' && *p <= '9' && n + 1 < sizeof digits) digits[n++] = (char)*p;
    digits[n] = '\0';
    if (n == 10 || n == 11) {
        if (cap < n + 4) return 0;
        snprintf(out, cap, "+55%s", digits);
    } else if (n >= 8 && n <= 15) {
        if (cap < n + 2) return 0;
        snprintf(out, cap, "+%s", digits);
    } else return 0;
    return validation_is_valid_phone(out);
}

int ctrl_teachers_create(const HttpRequest *req, HttpResponse *res)
{
    AuthSession session;
    int rc = coordinator(req, res, &session);
    if (rc != SUCCESS) return rc;
    char name[121] = {0}, raw_phone[64] = {0}, phone[32] = {0}, email[256] = {0};
    char password[128] = {0};
    if (!field(req->body, "name", name, sizeof name) ||
        !field(req->body, "whatsapp", raw_phone, sizeof raw_phone) ||
        !field(req->body, "password", password, sizeof password))
        return http_respond_error(res, 400, "name, phone and password are required");
    if (!validation_length_between(password, 8, 127))
        return http_respond_error(res, 400, "invalid password length");
    if (!field(req->body, "email", email, sizeof email)) email[0] = '\0';
    if (!canonical_phone(raw_phone, phone, sizeof phone))
        return http_respond_error(res, 400, "invalid phone");
    if (email[0] && (strlen(email) > 254 || !strchr(email, '@')))
        return http_respond_error(res, 400, "invalid email");
    if (db_begin() != SUCCESS) return http_respond_error(res, 500, "database error");
    User user;
    rc = users_create(name, phone, USER_TYPE_PROF, password, &user);
    if (rc == ERROR_ALREADY_EXISTS) {
        (void)db_rollback();
        return http_respond_error(res, 409, "phone already registered");
    }
    if (rc != SUCCESS) {
        (void)db_rollback();
        return http_respond_error(res, 400, "create failed");
    }
    sqlite3_stmt *st = NULL;
    if (db_prepare("UPDATE users SET email=? WHERE id=?;", &st) != SUCCESS) {
        (void)db_rollback();
        return http_respond_error(res, 500, "database error");
    }
    if (email[0]) db_bind_text(st, 1, email); else db_bind_null(st, 1);
    db_bind_int64(st, 2, (long long)user.id);
    int ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    if (!ok) {
        (void)db_rollback();
        return http_respond_error(res, 500, "database error");
    }
    if (db_commit() != SUCCESS) {
        (void)db_rollback();
        return http_respond_error(res, 500, "database error");
    }
    char safe_name[256], safe_email[512], output[900];
    json_string(user.name, safe_name, sizeof safe_name);
    json_string(email, safe_email, sizeof safe_email);
    snprintf(output, sizeof output,
        "{\"id\":%lld,\"name\":\"%s\",\"phone\":\"%s\",\"email\":\"%s\"}",
        (long long)user.id, safe_name, user.phone, safe_email);
    return http_respond_json(res, 201, output);
}

int ctrl_teachers_list(const HttpRequest *req, HttpResponse *res)
{
    AuthSession session;
    int rc = coordinator(req, res, &session);
    if (rc != SUCCESS) return rc;
    sqlite3_stmt *st = NULL;
    if (db_prepare("SELECT u.id,t.id,u.name,u.phone,COALESCE(u.email,''),u.created_at "
                   "FROM users u JOIN teachers t ON t.user_id=u.id "
                   "WHERE u.user_type='PROF' ORDER BY u.name;", &st) != SUCCESS)
        return http_respond_error(res, 500, "database error");
    size_t cap = 1024, used = 0; char *json = malloc(cap);
    if (!json) { sqlite3_finalize(st); return http_respond_error(res, 500, "oom"); }
    used += (size_t)snprintf(json + used, cap - used, "{\"items\":[");
    int count = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        char name[256], email[512], created[96];
        json_string((const char *)sqlite3_column_text(st, 2), name, sizeof name);
        json_string((const char *)sqlite3_column_text(st, 4), email, sizeof email);
        json_string((const char *)sqlite3_column_text(st, 5), created, sizeof created);
        if (used + 1000 >= cap) { cap *= 2; char *p = realloc(json, cap); if (!p) { free(json); sqlite3_finalize(st); return http_respond_error(res, 500, "oom"); } json = p; }
        used += (size_t)snprintf(json + used, cap - used,
            "%s{\"id\":%lld,\"teacher_id\":%lld,\"name\":\"%s\",\"phone\":\"%s\",\"email\":\"%s\",\"created_at\":\"%s\"}",
            count++ ? "," : "", (long long)sqlite3_column_int64(st, 0),
            (long long)sqlite3_column_int64(st, 1), name,
            (const char *)sqlite3_column_text(st, 3), email, created);
    }
    sqlite3_finalize(st);
    used += (size_t)snprintf(json + used, cap - used, "],\"count\":%d}", count);
    rc = http_respond_json(res, 200, json); free(json); return rc;
}

int ctrl_teachers_delete(const HttpRequest *req, HttpResponse *res, const char *id)
{
    AuthSession session;
    int rc = coordinator(req, res, &session);
    if (rc != SUCCESS) return rc;
    char *end = NULL; long long user_id = strtoll(id, &end, 10);
    if (!end || *end || user_id <= 0) return http_respond_error(res, 400, "invalid id");
    sqlite3_stmt *st = NULL;
    if (db_prepare("SELECT user_id FROM teachers WHERE user_id=?;", &st) != SUCCESS)
        return http_respond_error(res, 500, "database error");
    db_bind_int64(st, 1, user_id);
    int exists = sqlite3_step(st) == SQLITE_ROW; sqlite3_finalize(st);
    if (!exists) return http_respond_error(res, 404, "not found");
    rc = users_delete(user_id);
    if (rc != SUCCESS) return http_respond_error(res, 400, "delete failed");
    return http_respond_json(res, 200, "{\"ok\":true}");
}
