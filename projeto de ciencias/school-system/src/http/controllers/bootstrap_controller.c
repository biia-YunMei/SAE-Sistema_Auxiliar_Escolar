#include "http_server.h"
#include "database.h"
#include "users.h"
#include "validation.h"
#include "controllers.h"

#include <stdio.h>
#include <string.h>

static int field(const char *body, const char *key, char *out, size_t cap)
{
    char needle[64];
    int n = snprintf(needle, sizeof needle, "\"%s\":\"", key);
    if (!body || n < 0 || (size_t)n >= sizeof needle) return 0;
    const char *p = strstr(body, needle);
    if (!p) return 0;
    p += n;
    const char *end = strchr(p, '"');
    if (!end || (size_t)(end - p) >= cap) return 0;
    memcpy(out, p, (size_t)(end - p));
    out[end - p] = '\0';
    return 1;
}

int ctrl_bootstrap_admin(const HttpRequest *req, HttpResponse *res)
{
    if (strcmp(req->method, "POST") != 0)
        return http_respond_error(res, 405, "method not allowed");
    sqlite3_stmt *st = NULL;
    if (db_prepare("SELECT 1 FROM users WHERE user_type='COORD' LIMIT 1;", &st) != SUCCESS)
        return http_respond_error(res, 500, "database error");
    int exists = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
    if (exists) return http_respond_error(res, 409, "administrator already exists");

    char name[121] = {0}, phone[32] = {0}, password[128] = {0};
    if (!field(req->body, "name", name, sizeof name) ||
        !field(req->body, "phone", phone, sizeof phone) ||
        !field(req->body, "password", password, sizeof password))
        return http_respond_error(res, 400, "name, phone and password are required");
    if (!validation_is_valid_phone(phone)) return http_respond_error(res, 400, "invalid phone");
    User admin;
    int rc = users_create(name, phone, USER_TYPE_COORD, password, &admin);
    if (rc != SUCCESS) return http_respond_error(res, 400, "could not create administrator");
    return http_respond_json(res, 201, "{\"ok\":true,\"message\":\"administrator created\"}");
}
