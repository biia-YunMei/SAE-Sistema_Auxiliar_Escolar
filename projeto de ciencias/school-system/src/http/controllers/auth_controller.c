/* auth_controller.c
 * POST /api/auth  body: {"phone":"+55...","password":"..."}
 */
#include "http_server.h"
#include "auth.h"
#include "users.h"
#include "validation.h"
#include "logger.h"
#include "controllers.h"
#include "json_util.h"

#include <string.h>
#include <stdio.h>

/* Parsing JSON mínimo: aqui aceitamos um formato simples
 * {"phone":"+55...","password":"..."} sem dependência externa.
 * Para a feira de ciências isso basta; a Etapa 7 (cJSON) generaliza. */
static int extract_field(const char *body, const char *key, char *out, size_t cap)
{
    return json_object_get_string(body, key, out, cap);
}

int ctrl_auth_login(const HttpRequest *req, HttpResponse *res)
{
    if (strcmp(req->method, "POST") != 0)
        return http_respond_error(res, 405, "method not allowed");

    char phone[32] = {0}, password[128] = {0};
    if (!extract_field(req->body, "phone", phone, sizeof phone))
        return http_respond_error(res, 400, "missing phone");
    if (!validation_is_valid_phone(phone))
        return http_respond_error(res, 400, "invalid phone");
    extract_field(req->body, "password", password, sizeof password);

    AuthSession s;
    int rc = auth_login_phone_from_ip(phone, password[0] ? password : NULL,
                                      req->client_ip, &s);
    if (rc != SUCCESS) {
        if (rc == ERROR_FORBIDDEN)
            return http_respond_error(res, 429, "too many login attempts; try again later");
        if (rc == ERROR_UNAUTHORIZED)
            return http_respond_error(res, 401, "invalid credentials");
        return http_respond_error(res, 500, "login failed");
    }
    char token[80];
    if (auth_session_create(&s, token, sizeof token) != SUCCESS)
        return http_respond_error(res, 500, "could not create session");

    char body[256];
    snprintf(body, sizeof body,
        "{\"token\":\"%s\",\"user_id\":%lld,\"type\":\"%s\",\"expires_at\":%ld}",
        token, (long long)s.user_id,
        user_type_str(s.type), s.expires_at);
    return http_respond_json(res, 200, body);
}

/* POST /api/auth/logout: invalida o token que chegou no Authorization. */
int ctrl_auth_logout(const HttpRequest *req, HttpResponse *res)
{
    if (strcmp(req->method, "POST") != 0)
        return http_respond_error(res, 405, "method not allowed");
    if (!req->auth)
        return http_respond_error(res, 401, "missing authorization");
    if (auth_session_revoke(req->auth) != SUCCESS)
        return http_respond_error(res, 401, "invalid session");
    return http_respond_json(res, 200, "{\"ok\":true}");
}
