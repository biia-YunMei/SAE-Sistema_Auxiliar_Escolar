/* controllers.h
 * Exporta os handlers HTTP.
 *
 * Para rotas com path parameters, declaramos wrappers que recebem
 * também o id_str capturado pelo router.
 */
#ifndef SCHOOL_CONTROLLERS_H
#define SCHOOL_CONTROLLERS_H

#include "http_server.h"

int ctrl_auth_login        (const HttpRequest *req, HttpResponse *res);
int ctrl_auth_logout       (const HttpRequest *req, HttpResponse *res);
int ctrl_bootstrap_admin   (const HttpRequest *req, HttpResponse *res);
int ctrl_bootstrap_status  (const HttpRequest *req, HttpResponse *res);
int ctrl_notices_list      (const HttpRequest *req, HttpResponse *res);
int ctrl_notices_create    (const HttpRequest *req, HttpResponse *res);
int ctrl_notices_update    (const HttpRequest *req, HttpResponse *res, const char *id);
int ctrl_notices_delete    (const HttpRequest *req, HttpResponse *res, const char *id);

int ctrl_users_create      (const HttpRequest *req, HttpResponse *res);
int ctrl_users_list        (const HttpRequest *req, HttpResponse *res);
int ctrl_users_update      (const HttpRequest *req, HttpResponse *res, const char *id);
int ctrl_users_delete      (const HttpRequest *req, HttpResponse *res, const char *id);
int ctrl_teachers_create   (const HttpRequest *req, HttpResponse *res);
int ctrl_teachers_list     (const HttpRequest *req, HttpResponse *res);
int ctrl_teachers_delete   (const HttpRequest *req, HttpResponse *res, const char *id);
int ctrl_students_assign_class(const HttpRequest *req, HttpResponse *res);
int ctrl_students_create(const HttpRequest *req, HttpResponse *res);
int ctrl_students_list(const HttpRequest *req, HttpResponse *res);
int ctrl_messages_list(const HttpRequest *req, HttpResponse *res);
int ctrl_anonymous_enrollment_code(const HttpRequest *req, HttpResponse *res);

int ctrl_classes_list      (const HttpRequest *req, HttpResponse *res);
int ctrl_classes_create    (const HttpRequest *req, HttpResponse *res);
int ctrl_classes_update    (const HttpRequest *req, HttpResponse *res, const char *id);
int ctrl_classes_delete    (const HttpRequest *req, HttpResponse *res, const char *id);
int ctrl_classes_add_teacher (const HttpRequest *req, HttpResponse *res, const char *class_id);
int ctrl_classes_remove_teacher(const HttpRequest *req, HttpResponse *res,
                                const char *class_id, const char *teacher_id);

int ctrl_tasks_list        (const HttpRequest *req, HttpResponse *res);
int ctrl_tasks_create      (const HttpRequest *req, HttpResponse *res);
int ctrl_tasks_update      (const HttpRequest *req, HttpResponse *res, const char *id);
int ctrl_tasks_delete      (const HttpRequest *req, HttpResponse *res, const char *id);

/* Adaptadores das rotas que possuem parâmetros no caminho. */
int wr_tasks_update          (const HttpRequest *req, HttpResponse *res);
int wr_tasks_delete          (const HttpRequest *req, HttpResponse *res);
int wr_notices_delete        (const HttpRequest *req, HttpResponse *res);
int wr_notices_update        (const HttpRequest *req, HttpResponse *res);
int wr_classes_add_teacher   (const HttpRequest *req, HttpResponse *res);
int wr_classes_remove_teacher(const HttpRequest *req, HttpResponse *res);
int wr_classes_update        (const HttpRequest *req, HttpResponse *res);
int wr_classes_delete        (const HttpRequest *req, HttpResponse *res);
int wr_users_update          (const HttpRequest *req, HttpResponse *res);
int wr_users_delete          (const HttpRequest *req, HttpResponse *res);
int wr_teachers_delete       (const HttpRequest *req, HttpResponse *res);

int ctrl_whatsapp_webhook  (const HttpRequest *req, HttpResponse *res);
int ctrl_whatsapp_send     (const HttpRequest *req, HttpResponse *res);
int ctrl_whatsapp_notify_class(const HttpRequest *req, HttpResponse *res);
int ctrl_health            (const HttpRequest *req, HttpResponse *res);

#endif /* SCHOOL_CONTROLLERS_H */
