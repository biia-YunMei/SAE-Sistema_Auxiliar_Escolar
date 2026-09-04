/* http_server.h
 * Servidor HTTP/1.1 mínimo em C puro (POSIX sockets + threads).
 *
 * Decisão de projeto: para manter o sistema 100% C sem dependências
 * externas obrigatórias, implementamos um servidor HTTP/1.1 enxuto
 * usando sockets BSD e pthreads. Suporta os métodos necessários
 * (GET, POST, PUT, DELETE) com Content-Length e body.
 *
 * Em produção com muito tráfego, é possível trocar por
 * libmicrohttpd/mongoose sem alterar controllers (interface Router).
 */
#ifndef SCHOOL_HTTP_SERVER_H
#define SCHOOL_HTTP_SERVER_H

#include "error.h"
#include <stddef.h>

typedef struct {
    const char *method;     /* GET, POST, ... */
    const char *path;       /* /api/tasks */
    const char *query;      /* ?a=b (após ?) */
    const char *body;       /* corpo (pode ser "") */
    size_t      body_len;
    const char *auth;       /* valor do header Authorization (sem "Bearer ") */
    const char *webhook_signature; /* X-Hub-Signature-256, se houver */
    char        auth_storage[512];
    char        webhook_signature_storage[256];
    void       *user_ctx;   /* contexto passado no server_run */
    /* Armazenamento da requisição, gerenciado pelo servidor. */
    char       *storage;
} HttpRequest;

typedef struct {
    int         status;     /* 200, 201, 400, ... */
    const char *content_type;
    const char *body;
    size_t      body_len;
    /* Evita que handlers retornem ponteiros para buffers locais/liberados. */
    char        body_storage[65536];
} HttpResponse;

/* Assinatura de um handler/controller. */
typedef int (*HttpHandler)(const HttpRequest *req, HttpResponse *res);

/* Inicia o servidor. Bloqueia até receber sinal de parada. */
int  http_server_run(const char *host, int port, void *ctx,
                     volatile int *should_stop);

/* Helpers para escrever resposta. O body é copiado para buffer interno. */
int http_respond_text (HttpResponse *res, int status, const char *text);
int http_respond_json (HttpResponse *res, int status, const char *json);
int http_respond_error(HttpResponse *res, int code, const char *msg);

#endif /* SCHOOL_HTTP_SERVER_H */
