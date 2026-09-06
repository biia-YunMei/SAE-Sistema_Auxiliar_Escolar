/* router.c
 * Roteador com suporte a path parameter ":id".
 *
 * Para padrões com ":", o id_out é preenchido com o valor capturado;
 * handlers que não necessitam ignoram o parâmetro.
 */
#include "router.h"
#include "http_server.h"
#include "auth.h"
#include "logger.h"

#include <string.h>

static int match_path(const char *pattern, const char *path,
                      char *id_out, size_t cap)
{
    const char *pp = pattern, *ap = path;
    while (*pp && *ap) {
        if (*pp == ':') {
            const char *name = ++pp;
            while (*pp && *pp != '/') pp++;
            (void)name;
            const char *val = ap;
            while (*ap && *ap != '/') ap++;
            size_t vlen = (size_t)(ap - val);
            if (id_out && cap > vlen) {
                if (vlen >= cap) vlen = cap - 1;
                memcpy(id_out, val, vlen);
                id_out[vlen] = '\0';
            }
            continue;
        }
        if (*pp != *ap) return 0;
        pp++; ap++;
    }
    return *pp == 0 && *ap == 0;
}

/* Apenas os endpoints necessários antes da autenticação, ou acionados por
 * serviços externos, ficam públicos. Toda rota de uso do sistema passa por
 * esta barreira, além das permissões específicas dos seus controllers. */
static int route_is_public(const Route *route)
{
    return (strcmp(route->pattern, "/api/health") == 0) ||
           (strcmp(route->pattern, "/api/auth") == 0) ||
           (strcmp(route->pattern, "/api/bootstrap") == 0) ||
           (strcmp(route->pattern, "/api/whatsapp/webhook") == 0);
}

int router_dispatch(const Route *routes, size_t n,
                    const HttpRequest *req, HttpResponse *res,
                    char *id_out, size_t cap)
{
    for (size_t i = 0; i < n; i++) {
        if (strcmp(routes[i].method, req->method) != 0) continue;
        if (match_path(routes[i].pattern, req->path, id_out, cap)) {
            if (!route_is_public(&routes[i])) {
                AuthSession session;
                if (!req->auth)
                    return http_respond_error(res, 401, "missing authorization");
                if (auth_session_resolve(req->auth, &session) != SUCCESS)
                    return http_respond_error(res, 401, "invalid session");
            }
            return routes[i].handler(req, res);
        }
    }
    return http_respond_error(res, 404, "route not found");
}

/* Ponte para o server.c — passa o array de rotas. */
int router_dispatch_request_impl(void *server_ctx,
    const HttpRequest *req, HttpResponse *res, char *id_out, size_t cap)
{
    struct ServerRoutes { const Route *routes; size_t n; };
    struct ServerRoutes *sr = (struct ServerRoutes *)server_ctx;
    return router_dispatch(sr->routes, sr->n, req, res, id_out, cap);
}
