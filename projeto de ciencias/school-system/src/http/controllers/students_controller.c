/* Operacoes administrativas de aluno que nao pertencem ao front-end. */
#include "http_server.h"
#include "auth.h"
#include "database.h"
#include "users.h"
#include "classes.h"
#include "validation.h"
#include "controllers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static long long json_int(const char *body, const char *key);

static int student_field(const char *body, const char *key, char *out, size_t cap)
{
    char needle[64];
    int n = snprintf(needle, sizeof needle, "\"%s\":\"", key);
    if (!body || n < 0 || (size_t)n >= sizeof needle || !out || cap == 0) return 0;
    const char *p = strstr(body, needle);
    if (!p) return 0;
    p += n;
    size_t used = 0;
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) ++p;
        if (used + 1 >= cap) return 0;
        out[used++] = *p++;
    }
    if (*p != '"') return 0;
    out[used] = '\0';
    return 1;
}

static void student_json_string(const char *value, char *out, size_t cap)
{
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)(value ? value : ""); *p; ++p) {
        if ((*p == '"' || *p == '\\') && used + 2 < cap) out[used++] = '\\';
        if (*p >= 0x20 && used + 1 < cap) out[used++] = (char)*p;
    }
    if (cap) out[used] = '\0';
}

static int student_phone(const char *input, char out[32])
{
    char digits[24]; size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)input; *p; ++p)
        if (isdigit(*p) && n + 1 < sizeof digits) digits[n++] = (char)*p;
    digits[n] = '\0';
    if (n == 10 || n == 11) snprintf(out, 32, "+55%s", digits);
    else if (n >= 8 && n <= 15) snprintf(out, 32, "+%s", digits);
    else return 0;
    return validation_is_valid_phone(out);
}

static int student_coordinator(const HttpRequest *req, HttpResponse *res)
{
    AuthSession session;
    if (!req->auth || auth_session_resolve(req->auth, &session) != SUCCESS) {
        (void)http_respond_error(res, 401, "invalid session");
        return 0;
    }
    if (session.type != USER_TYPE_COORD) {
        (void)http_respond_error(res, 403, "coordinator only");
        return 0;
    }
    return 1;
}

int ctrl_students_create(const HttpRequest *req, HttpResponse *res)
{
    if (!student_coordinator(req, res)) return SUCCESS;

    char name[121] = {0}, raw_phone[64] = {0}, phone[32] = {0};
    if (!student_field(req->body, "name", name, sizeof name) ||
        !student_field(req->body, "phone", raw_phone, sizeof raw_phone))
        return http_respond_error(res, 400, "name, phone and class_id are required");
    long long class_id = json_int(req->body, "class_id");
    if (!validation_length_between(name, 1, 120) || class_id <= 0)
        return http_respond_error(res, 400, "invalid student data");
    if (!student_phone(raw_phone, phone))
        return http_respond_error(res, 400, "invalid phone");
    Class classroom;
    if (classes_find_by_id(class_id, &classroom) != SUCCESS || strcmp(classroom.status, "ACTIVE") != 0)
        return http_respond_error(res, 404, "class not found");

    if (db_begin() != SUCCESS) return http_respond_error(res, 500, "database error");
    User student;
    int rc = users_create(name, phone, USER_TYPE_STUD, NULL, &student);
    if (rc == ERROR_ALREADY_EXISTS) {
        (void)db_rollback();
        return http_respond_error(res, 409, "phone already registered");
    }
    if (rc != SUCCESS) {
        (void)db_rollback();
        return http_respond_error(res, 400, "create failed");
    }

    sqlite3_stmt *statement = NULL;
    if (db_prepare("UPDATE students SET class_id=?,grade=? WHERE user_id=?;", &statement) != SUCCESS) {
        (void)db_rollback();
        return http_respond_error(res, 500, "database error");
    }
    db_bind_int64(statement, 1, class_id);
    db_bind_text(statement, 2, classroom.grade);
    db_bind_int64(statement, 3, (long long)student.id);
    int saved = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(database_get()) == 1;
    sqlite3_finalize(statement);
    if (!saved) {
        (void)db_rollback();
        return http_respond_error(res, 500, "database error");
    }
    if (db_commit() != SUCCESS) {
        (void)db_rollback();
        return http_respond_error(res, 500, "database error");
    }

    char safe_name[256], safe_grade[96], output[512];
    student_json_string(student.name, safe_name, sizeof safe_name);
    student_json_string(classroom.grade, safe_grade, sizeof safe_grade);
    snprintf(output, sizeof output,
        "{\"id\":%lld,\"name\":\"%s\",\"phone\":\"%s\",\"class_id\":%lld,\"grade\":\"%s\"}",
        (long long)student.id, safe_name, student.phone, class_id, safe_grade);
    return http_respond_json(res, 201, output);
}

int ctrl_students_list(const HttpRequest *req, HttpResponse *res)
{
    if (!student_coordinator(req, res)) return SUCCESS;
    sqlite3_stmt *statement = NULL;
    if (db_prepare("SELECT u.id,u.name,u.phone,COALESCE(s.grade,''),COALESCE(s.class_id,0),COALESCE(c.name,'') "
                   "FROM users u JOIN students s ON s.user_id=u.id "
                   "LEFT JOIN classes c ON c.id=s.class_id "
                   "WHERE u.user_type='STUD' ORDER BY u.name;", &statement) != SUCCESS)
        return http_respond_error(res, 500, "database error");

    size_t capacity = 1024, used = 0; int count = 0;
    char *json = malloc(capacity);
    if (!json) { sqlite3_finalize(statement); return http_respond_error(res, 500, "oom"); }
    used += (size_t)snprintf(json + used, capacity - used, "{\"items\":[");
    while (sqlite3_step(statement) == SQLITE_ROW) {
        if (used + 2048 >= capacity) {
            capacity *= 2;
            char *grown = realloc(json, capacity);
            if (!grown) { free(json); sqlite3_finalize(statement); return http_respond_error(res, 500, "oom"); }
            json = grown;
        }
        char name[256], grade[96], class_name[160];
        student_json_string((const char *)sqlite3_column_text(statement, 1), name, sizeof name);
        student_json_string((const char *)sqlite3_column_text(statement, 3), grade, sizeof grade);
        student_json_string((const char *)sqlite3_column_text(statement, 5), class_name, sizeof class_name);
        used += (size_t)snprintf(json + used, capacity - used,
            "%s{\"id\":%lld,\"name\":\"%s\",\"phone\":\"%s\",\"class_id\":%lld,\"class_name\":\"%s\",\"grade\":\"%s\"}",
            count++ ? "," : "", (long long)sqlite3_column_int64(statement, 0), name,
            (const char *)sqlite3_column_text(statement, 2),
            (long long)sqlite3_column_int64(statement, 4), class_name, grade);
    }
    sqlite3_finalize(statement);
    used += (size_t)snprintf(json + used, capacity - used, "],\"count\":%d}", count);
    int rc = http_respond_json(res, 200, json);
    free(json);
    return rc;
}

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
    Class classroom;
    if (classes_find_by_id(class_id, &classroom) != SUCCESS || strcmp(classroom.status, "ACTIVE") != 0)
        return http_respond_error(res, 404, "class not found");
    if (db_prepare("UPDATE students SET class_id=?,grade=? WHERE user_id=?;", &statement) != SUCCESS)
        return http_respond_error(res, 500, "database error");
    db_bind_int64(statement, 1, class_id);
    db_bind_text(statement, 2, classroom.grade);
    db_bind_int64(statement, 3, user_id);
    int result = sqlite3_step(statement);
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE || sqlite3_changes(database_get()) != 1)
        return http_respond_error(res, 400, "could not assign class");
    return http_respond_json(res, 200, "{\"status\":\"assigned\"}");
}
