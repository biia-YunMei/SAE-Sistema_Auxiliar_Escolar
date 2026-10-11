#include "anonymous_enrollment.h"
#include "auth.h"
#include "database.h"
#include "http_server.h"
#include "controllers.h"
#include "json_util.h"

#include <stdio.h>
#include <string.h>

int ctrl_anonymous_enrollment_code(const HttpRequest *req, HttpResponse *res)
{
    if (strcmp(req->method, "POST") != 0)
        return http_respond_error(res, 405, "method not allowed");
    AuthSession session;
    if (!req->auth || auth_session_resolve(req->auth, &session) != SUCCESS)
        return http_respond_error(res, 401, "invalid session");
    if (session.type != USER_TYPE_COORD)
        return http_respond_error(res, 403, "coordinator only");

    int64_t class_id = 0;
    if (!json_object_get_int64(req->body, "class_id", &class_id) || class_id <= 0)
        return http_respond_error(res, 400, "class_id required");

    char code[17];
    int rc = anonymous_enrollment_create(session.user_id, class_id, code);
    if (rc == ERROR_NOT_FOUND)
        return http_respond_error(res, 404, "class not found");
    if (rc == ERROR_CONFIG)
        return http_respond_error(res, 503, "secure random generator unavailable");
    if (rc != SUCCESS)
        return http_respond_error(res, 500, "could not create enrollment code");
    char response[128];
    (void)snprintf(response, sizeof response,
        "{\"code\":\"%s\",\"expires_in_hours\":24}", code);
    return http_respond_json(res, 201, response);
}
