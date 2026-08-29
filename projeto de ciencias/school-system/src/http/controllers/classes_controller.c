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
#include "validation.h"
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
static long long extract_int(const char *b, const char *k, long long d)
{
    char n[64]; int x = snprintf(n, sizeof n, "\"%s\":", k);
    if (x <= 0 || (size_t)x >= sizeof n) return d;
    const char *p = strstr(b, n);
    if (!p) return d;
    p += x;
    while (*p == ' ' || *p == '\t') p++;
    char *end = NULL;
    long long v = strtoll(p, &end, 10);
    return (end == p) ? d : v;
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

int ctrl_classes_list(const HttpRequest *req, HttpResponse *res)
{
    AuthSession s;
    if (!req->auth) return http_respond_error(res, 401, "missing authorization");
    if (auth_session_resolve(req->auth, &s) != SUCCESS)
        return http_respond_error(res, 401, "invalid session");

    Class *list = NULL; size_t n = 0;
    if (classes_list(&list, &n) != SUCCESS)
        return http_respond_error(res, 500, "list failed");

    size_t cap = 256 + n * 512;
    char *out = (char *)malloc(cap);
    if (!out) { free(list); return http_respond_error(res, 500, "oom"); }
    size_t p = 0;
    p += (size_t)snprintf(out + p, cap - p, "{\"count\":%zu,\"items\":[", n);
    for (size_t i = 0; i < n; i++) {
        p += (size_t)snprintf(out + p, cap - p,
            "%s{\"id\":%lld,\"name\":\"%s\",\"grade\":\"%s\","
            "\"shift\":\"%s\",\"status\":\"%s\"}",
            i ? "," : "",
            (long long)list[i].id, list[i].name, list[i].grade,
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

    Class c;
    rc = classes_create(name, grade, class_shift_from(shift_s[0] ? shift_s : "MORNING"), &c);
    if (rc == ERROR_ALREADY_EXISTS) return http_respond_error(res, 409, "class exists");
    if (rc != SUCCESS)              return http_respond_error(res, 400, "create failed");

    char out[256];
    snprintf(out, sizeof out,
        "{\"id\":%lld,\"name\":\"%s\",\"grade\":\"%s\",\"shift\":\"%s\"}",
        (long long)c.id, c.name, c.grade, class_shift_str(c.shift));
    return http_respond_json(res, 201, out);
}

int ctrl_classes_add_teacher(const HttpRequest *req, HttpResponse *res, const char *id_str)
{
    AuthSession s;
    int rc = require_coord(req, res, &s);
    if (rc != SUCCESS) return rc;

    long long class_id = strtoll(id_str, NULL, 10);
    long long teacher_id = extract_int(req->body, "teacher_id", -1);
    if (class_id <= 0 || teacher_id <= 0)
        return http_respond_error(res, 400, "ids required");
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

    long long class_id   = strtoll(class_id_str,   NULL, 10);
    long long teacher_id = strtoll(teacher_id_str, NULL, 10);
    if (class_id <= 0 || teacher_id <= 0)
        return http_respond_error(res, 400, "ids required");
    if (classes_unlink_teacher(teacher_id, class_id) != SUCCESS)
        return http_respond_error(res, 400, "unlink failed");
    return http_respond_json(res, 200, "{\"ok\":true}");
}
