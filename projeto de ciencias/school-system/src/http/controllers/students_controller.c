/* Operacoes administrativas de aluno que nao pertencem ao front-end. */
#include "http_server.h"
#include "auth.h"
#include "database.h"
#include "users.h"
#include "controllers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long long json_int(const char *body, const char *key)
{
    char needle[64];
    int written = snprintf(needle, sizeof needle, "\"%s\":", key);
    const char *value = body ? strstr(body, needle) : NULL;
    if (written < 0 || !value) return 0;
    value += written;
    return strtoll(value, NULL, 10);
}

int ctrl_students_assign_class(const HttpRequest *req, HttpResponse *res)
{
    if (strcmp(req->method, "POST") != 0)
        return http_respond_error(res, 405, "method not allowed");

    AuthSession session;
    if (!req->auth || auth_session_resolve(req->auth, &session) != SUCCESS)
        return http_respond_error(res, 401, "invalid session");
    if (session.type != USER_TYPE_COORD)
        return http_respond_error(res, 403, "coordinator only");

    long long user_id = json_int(req->body, "user_id");
    long long class_id = json_int(req->body, "class_id");
    if (user_id <= 0 || class_id <= 0)
        return http_respond_error(res, 400, "user_id and class_id are required");

    User student;
    if (users_find_by_id(user_id, &student) != SUCCESS || student.type != USER_TYPE_STUD)
        return http_respond_error(res, 400, "student not found");

    sqlite3_stmt *statement = NULL;
    if (db_prepare("UPDATE students SET class_id=? WHERE user_id=?;", &statement) != SUCCESS)
        return http_respond_error(res, 500, "database error");
    db_bind_int64(statement, 1, class_id);
    db_bind_int64(statement, 2, user_id);
    int result = sqlite3_step(statement);
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE || sqlite3_changes(database_get()) != 1)
        return http_respond_error(res, 400, "could not assign class");
    return http_respond_json(res, 200, "{\"status\":\"assigned\"}");
}
