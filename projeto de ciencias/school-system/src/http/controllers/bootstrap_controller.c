#include "http_server.h"
#include "database.h"
#include "users.h"
#include "validation.h"
#include "config.h"
#include "logger.h"
#include "controllers.h"
#include "json_util.h"
#include <stdlib.h>

#include <stdio.h>
#include <string.h>

static int bootstrap_has_any_user(int *exists)
{
    if (!exists) return ERROR_INVALID_INPUT;
    *exists = 0;
    sqlite3_stmt *st = NULL;
    if (db_prepare("SELECT 1 FROM users LIMIT 1;", &st) != SUCCESS)
        return ERROR_DATABASE;
    int step = sqlite3_step(st);
    if (step == SQLITE_ROW) *exists = 1;
    sqlite3_finalize(st);
    return step == SQLITE_ROW || step == SQLITE_DONE ? SUCCESS : ERROR_DATABASE;
}

int ctrl_bootstrap_status(const HttpRequest *req, HttpResponse *res)
{
    if (strcmp(req->method, "GET") != 0)
        return http_respond_error(res, 405, "method not allowed");
    int exists = 0;
    if (bootstrap_has_any_user(&exists) != SUCCESS)
        return http_respond_error(res, 500, "could not verify initial setup");
    if (exists && config_clear_bootstrap_key() != SUCCESS)
        log_warn("bootstrap: não foi possível remover a chave do arquivo .env");
    if (exists)
        return http_respond_json(res, 200,
            "{\"available\":false,\"reason\":\"user_exists\"}");
    const char *key = getenv("SAE_BOOTSTRAP_KEY");
    if (!key || strlen(key) < 32)
        return http_respond_json(res, 200,
            "{\"available\":false,\"reason\":\"bootstrap_disabled\"}");
    return http_respond_json(res, 200, "{\"available\":true}");
}

static int field(const char *body, const char *key, char *out, size_t cap)
{
    return json_object_get_string(body, key, out, cap);
}

int ctrl_bootstrap_admin(const HttpRequest *req, HttpResponse *res)
{
    if (strcmp(req->method, "POST") != 0)
        return http_respond_error(res, 405, "method not allowed");
    char name[121] = {0}, phone[32] = {0}, password[128] = {0}, key[128] = {0};
    if (!field(req->body, "name", name, sizeof name) ||
        !field(req->body, "phone", phone, sizeof phone) ||
        !field(req->body, "password", password, sizeof password) ||
        !field(req->body, "bootstrap_key", key, sizeof key))
        return http_respond_error(res, 400, "name, phone, password and setup key are required");
    const char *expected = getenv("SAE_BOOTSTRAP_KEY");
    if (!expected || !*expected)
        return http_respond_error(res, 403, "bootstrap is disabled; configure SAE_BOOTSTRAP_KEY");
    size_t expected_len = strlen(expected), supplied_len = strlen(key);
    if (expected_len < 32)
        return http_respond_error(res, 403, "SAE_BOOTSTRAP_KEY must contain at least 32 characters");
    unsigned int key_diff = (unsigned int)(expected_len ^ supplied_len);
    for (size_t i = 0; i < expected_len; ++i)
        key_diff |= (unsigned char)expected[i] ^
                    (i < supplied_len ? (unsigned char)key[i] : 0);
    if (key_diff != 0)
        return http_respond_error(res, 403, "invalid setup key");
    if (!validation_is_valid_phone(phone)) return http_respond_error(res, 400, "invalid phone");
    if (!validation_length_between(name, 1, 120) || !validation_length_between(password, 8, 127))
        return http_respond_error(res, 400, "invalid name or password length");
    if (db_begin() != SUCCESS) return http_respond_error(res, 500, "database error");
    sqlite3_stmt *st = NULL;
    if (db_prepare("SELECT 1 FROM users LIMIT 1;", &st) != SUCCESS) {
        (void)db_rollback();
        return http_respond_error(res, 500, "database error");
    }
    int exists = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
    if (exists) {
        (void)db_rollback();
        if (config_clear_bootstrap_key() != SUCCESS)
            log_warn("bootstrap: não foi possível remover a chave do arquivo .env");
        return http_respond_error(res, 409, "administrator already exists");
    }
    User admin;
    int rc = users_create(name, phone, USER_TYPE_COORD, password, &admin);
    if (rc != SUCCESS) {
        (void)db_rollback();
        return http_respond_error(res, 400, "could not create administrator");
    }
    if (db_commit() != SUCCESS) {
        (void)db_rollback();
        return http_respond_error(res, 500, "database error");
    }
    if (config_clear_bootstrap_key() != SUCCESS)
        log_warn("bootstrap: administrador criado, mas a chave não pôde ser removida do arquivo .env");
    return http_respond_json(res, 201, "{\"ok\":true,\"message\":\"administrator created\"}");
}
