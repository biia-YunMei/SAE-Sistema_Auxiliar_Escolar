/* wrappers.c
 * Adapta handlers com path-parameters para a assinatura
 * uniforme (req, res) exigida pelo router.
 */
#include "http_server.h"
#include "controllers.h"

#include <string.h>

int wr_tasks_update(const HttpRequest *req, HttpResponse *res)
{
    /* O router preenche o path inteiro; extraímos o último segmento. */
    const char *p = req->path + strlen("/api/tasks/");
    if (!*p) return http_respond_error(res, 400, "id required");
    return ctrl_tasks_update(req, res, p);
}

int wr_tasks_delete(const HttpRequest *req, HttpResponse *res)
{
    const char *p = req->path + strlen("/api/tasks/");
    if (!*p) return http_respond_error(res, 400, "id required");
    return ctrl_tasks_delete(req, res, p);
}

int wr_notices_delete(const HttpRequest *req, HttpResponse *res)
{
    const char *id = req->path + strlen("/api/notices/");
    return ctrl_notices_delete(req, res, id);
}

int wr_notices_update(const HttpRequest *req, HttpResponse *res)
{
    const char *id = req->path + strlen("/api/notices/");
    return ctrl_notices_update(req, res, id);
}

int wr_users_update(const HttpRequest *req, HttpResponse *res)
{
    const char *id = req->path + strlen("/api/users/");
    if (!*id) return http_respond_error(res, 400, "id required");
    return ctrl_users_update(req, res, id);
}

int wr_users_delete(const HttpRequest *req, HttpResponse *res)
{
    const char *id = req->path + strlen("/api/users/");
    if (!*id) return http_respond_error(res, 400, "id required");
    return ctrl_users_delete(req, res, id);
}

int wr_teachers_delete(const HttpRequest *req, HttpResponse *res)
{
    const char *id = req->path + strlen("/api/teachers/");
    return ctrl_teachers_delete(req, res, id);
}

int wr_classes_update(const HttpRequest *req, HttpResponse *res)
{
    const char *id = req->path + strlen("/api/classes/");
    if (!*id) return http_respond_error(res, 400, "id required");
    return ctrl_classes_update(req, res, id);
}

int wr_classes_delete(const HttpRequest *req, HttpResponse *res)
{
    const char *id = req->path + strlen("/api/classes/");
    if (!*id) return http_respond_error(res, 400, "id required");
    return ctrl_classes_delete(req, res, id);
}

int wr_classes_add_teacher(const HttpRequest *req, HttpResponse *res)
{
    const char *rest = req->path + strlen("/api/classes/");
    const char *slash = strchr(rest, '/');
    if (!slash || strcmp(slash, "/teachers") != 0)
        return http_respond_error(res, 400, "class_id required");

    size_t id_length = (size_t)(slash - rest);
    char class_id[32];
    if (id_length == 0 || id_length >= sizeof class_id)
        return http_respond_error(res, 400, "invalid class id");
    memcpy(class_id, rest, id_length);
    class_id[id_length] = '\0';
    return ctrl_classes_add_teacher(req, res, class_id);
}

int wr_classes_remove_teacher(const HttpRequest *req, HttpResponse *res)
{
    const char *rest = req->path + strlen("/api/classes/");
    if (!*rest) return http_respond_error(res, 400, "ids required");
    char buf[64] = {0};
    const char *slash = strchr(rest, '/');
    if (!slash) return http_respond_error(res, 400, "ids required");
    size_t cid_len = (size_t)(slash - rest);
    if (cid_len >= sizeof buf) return http_respond_error(res, 400, "id too long");
    memcpy(buf, rest, cid_len);
    buf[cid_len] = '\0';
    /* Após o slash: "/teachers/<teacher_id>" */
    const char *t = slash + strlen("/teachers/");
    if (!*t) return http_respond_error(res, 400, "teacher_id required");
    return ctrl_classes_remove_teacher(req, res, buf, t);
}
