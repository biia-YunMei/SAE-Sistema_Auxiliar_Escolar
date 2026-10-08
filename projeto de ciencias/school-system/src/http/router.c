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
#include "config.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *url;
    const char *file;
    const char *content_type;
} StaticAsset;

/* Somente esses arquivos públicos são servidos; o restante do filesystem
 * nunca pode ser solicitado pela URL. */
static const StaticAsset static_assets[] = {
    { "/", "index.html", "text/html; charset=utf-8" },
    { "/index.html", "index.html", "text/html; charset=utf-8" },
    { "/setup.html", "setup.html", "text/html; charset=utf-8" },
    { "/admin-register.html", "admin-register.html", "text/html; charset=utf-8" },
    { "/professores.html", "professores.html", "text/html; charset=utf-8" },
    { "/coordenacao.html", "coordenacao.html", "text/html; charset=utf-8" },
    { "/css/style.css", "css/style.css", "text/css; charset=utf-8" },
    { "/js/api.js", "js/api.js", "text/javascript; charset=utf-8" },
    { "/js/script.js", "js/script.js", "text/javascript; charset=utf-8" }
};

static int serve_static_asset(const HttpRequest *req, HttpResponse *res)
{
    if (strcmp(req->method, "GET") != 0 || strncmp(req->path, "/api/", 5) == 0)
        return 0;
    const StaticAsset *asset = NULL;
    for (size_t i = 0; i < sizeof static_assets / sizeof static_assets[0]; ++i) {
        if (strcmp(req->path, static_assets[i].url) == 0) {
            asset = &static_assets[i];
            break;
        }
    }
    if (!asset) return 0;

    const AppConfig *cfg = config_current();
    char path[768];
    int n = cfg ? snprintf(path, sizeof path, "%s/%s", cfg->web_root, asset->file) : -1;
    if (n < 0 || (size_t)n >= sizeof path)
        return http_respond_error(res, 500, "web root configuration invalid") == SUCCESS;
    FILE *file = fopen(path, "rb");
    if (!file)
        return http_respond_error(res, 404, "frontend asset not found") == SUCCESS;
    size_t length = fread(res->body_storage, 1, sizeof res->body_storage, file);
    int too_large = !feof(file);
    int read_error = ferror(file);
    fclose(file);
    if (too_large || read_error)
        return http_respond_error(res, 500, "frontend asset could not be read") == SUCCESS;
    res->status = 200;
    res->content_type = asset->content_type;
    res->body = res->body_storage;
    res->body_len = length;
    return 1;
}

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
    if (serve_static_asset(req, res)) return SUCCESS;
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
