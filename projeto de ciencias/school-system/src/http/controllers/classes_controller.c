/* classes_controller.c
 * Endpoints:
 *   GET  /api/classes
 *   POST /api/classes
 *   POST /api/classes/:id/teachers  body: {teacher_id}
 *   DELETE /api/classes/:id/teachers/:teacher_id
 */
#include "http_server.h"
#include "auth.h"
#include "users.h"
#include "classes.h"
#include "database.h"
#include "validation.h"
#include "logger.h"
#include "controllers.h"
#include "json_util.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

static void class_json_escape(const char *value, char *out, size_t cap)
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
static long long extract_int(const char *b, const char *k, long long d)
{
    int64_t value;
    return json_object_get_int64(b, k, &value) ? (long long)value : d;
}

static int valid_shift(const char *shift)
{
    return shift && (strcmp(shift, "MORNING") == 0 || strcmp(shift, "AFTERNOON") == 0 ||
                     strcmp(shift, "EVENING") == 0 || strcmp(shift, "FULL") == 0);
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

static int require_coord_or_prof(const HttpRequest *req, HttpResponse *res, AuthSession *out)
{
    if (!req->auth || auth_session_resolve(req->auth, out) != SUCCESS) {
        (void)http_respond_error(res, 401, "invalid session");
        return ERROR_UNAUTHORIZED;
    }
    if (out->type != USER_TYPE_COORD && out->type != USER_TYPE_PROF) {
        (void)http_respond_error(res, 403, "forbidden");
        return ERROR_FORBIDDEN;
    }
    return SUCCESS;
}

static int teacher_id_for_user(int64_t user_id, long long *teacher_id)
{
    sqlite3_stmt *statement = NULL;
    if (!teacher_id || db_prepare("SELECT id FROM teachers WHERE user_id=?;", &statement) != SUCCESS)
        return ERROR_DATABASE;
    db_bind_int64(statement, 1, user_id);
    int found = sqlite3_step(statement) == SQLITE_ROW;
    if (found) *teacher_id = sqlite3_column_int64(statement, 0);
    sqlite3_finalize(statement);
    return found ? SUCCESS : ERROR_NOT_FOUND;
}

int ctrl_classes_list(const HttpRequest *req, HttpResponse *res)
{
    AuthSession s;
    int auth_rc = require_coord_or_prof(req, res, &s);
    if (auth_rc != SUCCESS) return auth_rc;

    Class *list = NULL; size_t n = 0;
    if (classes_list(&list, &n) != SUCCESS)
        return http_respond_error(res, 500, "list failed");

    if (s.type == USER_TYPE_PROF) {
        long long teacher_id = 0;
        if (teacher_id_for_user(s.user_id, &teacher_id) != SUCCESS) {
            free(list);
            return http_respond_error(res, 403, "teacher profile not found");
        }
        size_t kept = 0;
        for (size_t i = 0; i < n; ++i)
            if (strcmp(list[i].status, "ACTIVE") == 0 &&
                classes_teacher_has_access(teacher_id, list[i].id))
                list[kept++] = list[i];
        n = kept;
    }

    if (n > (SIZE_MAX - 256) / 1024) { free(list); return http_respond_error(res, 500, "response too large"); }
    size_t cap = 256 + n * 1024;
    char *out = (char *)malloc(cap);
    if (!out) { free(list); return http_respond_error(res, 500, "oom"); }
    size_t p = 0;
    p += (size_t)snprintf(out + p, cap - p, "{\"count\":%zu,\"items\":[", n);
    for (size_t i = 0; i < n; i++) {
        char name[128], grade[64];
        class_json_escape(list[i].name, name, sizeof name);
        class_json_escape(list[i].grade, grade, sizeof grade);
        p += (size_t)snprintf(out + p, cap - p,
            "%s{\"id\":%lld,\"name\":\"%s\",\"grade\":\"%s\","
            "\"shift\":\"%s\",\"status\":\"%s\"}",
            i ? "," : "",
            (long long)list[i].id, name, grade,
            class_shift_str(list[i].shift), list[i].status);
    }
    p += (size_t)snprintf(out + p, cap - p, "]}");
    free(list);
    int r = http_respond_json(res, 200, out);
    free(out);
    return r;
}

int ctrl_classes_create(const HttpRequest *req, HttpResponse *res)
{
    AuthSession s;
    int rc = require_coord(req, res, &s);
    if (rc != SUCCESS) return rc;

    char name[64]={0}, grade[32]={0}, shift_s[16]={0};
    if (!extract_str(req->body, "name",  name,  sizeof name))  return http_respond_error(res, 400, "name required");
    if (!extract_str(req->body, "grade", grade, sizeof grade)) return http_respond_error(res, 400, "grade required");
    extract_str(req->body, "shift", shift_s, sizeof shift_s);
    if (shift_s[0] && !valid_shift(shift_s))
        return http_respond_error(res, 400, "invalid shift");

    Class c;
    rc = classes_create(name, grade, class_shift_from(shift_s[0] ? shift_s : "MORNING"), &c);
    if (rc == ERROR_ALREADY_EXISTS) return http_respond_error(res, 409, "class exists");
    if (rc != SUCCESS)              return http_respond_error(res, 400, "create failed");

    char out[512], safe_name[128], safe_grade[64];
    class_json_escape(c.name, safe_name, sizeof safe_name);
    class_json_escape(c.grade, safe_grade, sizeof safe_grade);
    snprintf(out, sizeof out,
        "{\"id\":%lld,\"name\":\"%s\",\"grade\":\"%s\",\"shift\":\"%s\"}",
        (long long)c.id, safe_name, safe_grade, class_shift_str(c.shift));
    return http_respond_json(res, 201, out);
}

int ctrl_classes_update(const HttpRequest *req, HttpResponse *res, const char *id_str)
{
    AuthSession session;
    int rc = require_coord(req, res, &session);
    if (rc != SUCCESS) return rc;

    int64_t parsed_class_id;
    if (!parse_int64_strict(id_str, &parsed_class_id) || parsed_class_id <= 0)
        return http_respond_error(res, 400, "id required");
    long long class_id = (long long)parsed_class_id;

    char name[64] = {0}, grade[32] = {0}, shift_s[16] = {0};
    if (!extract_str(req->body, "name", name, sizeof name))
        return http_respond_error(res, 400, "name required");
    if (!extract_str(req->body, "grade", grade, sizeof grade))
        return http_respond_error(res, 400, "grade required");
    if (!extract_str(req->body, "shift", shift_s, sizeof shift_s))
        return http_respond_error(res, 400, "shift required");
    if (!valid_shift(shift_s))
        return http_respond_error(res, 400, "invalid shift");

    Class updated;
    rc = classes_update(class_id, name, grade, class_shift_from(shift_s), &updated);
    if (rc == ERROR_NOT_FOUND) return http_respond_error(res, 404, "class not found");
    if (rc == ERROR_ALREADY_EXISTS) return http_respond_error(res, 409, "class exists");
    if (rc != SUCCESS) return http_respond_error(res, 400, "update failed");
    return http_respond_json(res, 200, "{\"ok\":true,\"message\":\"class updated\"}");
}

int ctrl_classes_delete(const HttpRequest *req, HttpResponse *res, const char *id_str)
{
    AuthSession session;
    int rc = require_coord(req, res, &session);
    if (rc != SUCCESS) return rc;

    int64_t parsed_class_id;
    if (!parse_int64_strict(id_str, &parsed_class_id) || parsed_class_id <= 0)
        return http_respond_error(res, 400, "id required");
    long long class_id = (long long)parsed_class_id;
    rc = classes_delete(class_id);
    if (rc == ERROR_NOT_FOUND) return http_respond_error(res, 404, "class not found");
    if (rc != SUCCESS) return http_respond_error(res, 400, "delete failed");
    return http_respond_json(res, 200, "{\"ok\":true,\"message\":\"class deactivated\"}");
}

int ctrl_classes_add_teacher(const HttpRequest *req, HttpResponse *res, const char *id_str)
{
    AuthSession s;
    int rc = require_coord(req, res, &s);
    if (rc != SUCCESS) return rc;

    int64_t parsed_class_id;
    if (!parse_int64_strict(id_str, &parsed_class_id) || parsed_class_id <= 0)
        return http_respond_error(res, 400, "ids required");
    long long class_id = (long long)parsed_class_id;
    long long teacher_id = extract_int(req->body, "teacher_id", -1);
    long long teacher_user_id = extract_int(req->body, "teacher_user_id", -1);
    if (teacher_user_id > 0) {
        sqlite3_stmt *statement = NULL;
        if (db_prepare("SELECT id FROM teachers WHERE user_id=?;", &statement) != SUCCESS)
            return http_respond_error(res, 500, "database error");
        db_bind_int64(statement, 1, teacher_user_id);
        int step = sqlite3_step(statement);
        if (step == SQLITE_ROW) {
            teacher_id = sqlite3_column_int64(statement, 0);
        }
        sqlite3_finalize(statement);
        if (step == SQLITE_DONE)
            return http_respond_error(res, 404, "teacher user not found");
        if (step != SQLITE_ROW)
            return http_respond_error(res, 500, "database error");
    }
    if (class_id <= 0)
        return http_respond_error(res, 400, "invalid class id");
    if (teacher_id <= 0)
        return http_respond_error(res, 400, "teacher id required");
    if (classes_link_teacher(teacher_id, class_id) != SUCCESS)
        return http_respond_error(res, 400, "link failed");
    return http_respond_json(res, 200, "{\"ok\":true}");
}

int ctrl_classes_remove_teacher(const HttpRequest *req, HttpResponse *res,
                                 const char *class_id_str, const char *teacher_id_str)
{
    AuthSession s;
    int rc = require_coord(req, res, &s);
    if (rc != SUCCESS) return rc;

    int64_t parsed_class_id, parsed_teacher_id;
    if (!parse_int64_strict(class_id_str, &parsed_class_id) ||
        !parse_int64_strict(teacher_id_str, &parsed_teacher_id))
        return http_respond_error(res, 400, "ids required");
    long long class_id = (long long)parsed_class_id;
    long long teacher_id = (long long)parsed_teacher_id;
    if (class_id <= 0 || teacher_id <= 0)
        return http_respond_error(res, 400, "ids required");
    if (classes_unlink_teacher(teacher_id, class_id) != SUCCESS)
        return http_respond_error(res, 400, "unlink failed");
    return http_respond_json(res, 200, "{\"ok\":true}");
}
