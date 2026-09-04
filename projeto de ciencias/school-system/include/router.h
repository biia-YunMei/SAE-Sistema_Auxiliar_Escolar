/* router.h
 * Roteador simples: método + path → handler.
 *
 * Suporta path parameters com ":id" (ex: /api/tasks/:id).
 */
#ifndef SCHOOL_ROUTER_H
#define SCHOOL_ROUTER_H

#include "error.h"
#include "http_server.h"

typedef struct {
    char        method[8];
    char        pattern[128];
    HttpHandler handler;
} Route;

int router_dispatch(const Route *routes, size_t n,
                    const HttpRequest *req, HttpResponse *res,
                    char *id_out, size_t cap);

int router_dispatch_request_impl(void *server_ctx,
                                 const HttpRequest *req, HttpResponse *res,
                                 char *id_out, size_t cap);

#endif /* SCHOOL_ROUTER_H */
