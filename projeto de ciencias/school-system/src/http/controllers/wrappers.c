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

int wr_classes_add_teacher(const HttpRequest *req, HttpResponse *res)
{
    const char *p = req->path + strlen("/api/classes/");
    if (!*p) return http_respond_error(res, 400, "class_id required");
    return ctrl_classes_add_teacher(req, res, p);
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
