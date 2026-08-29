/* server.c
 * Servidor HTTP/1.1 mínimo em C puro.
 */
#include "http_server.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef int socklen_t;
  #define close closesocket
  #define strncasecmp _strnicmp
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <pthread.h>
  #include <signal.h>
  #include <errno.h>
  #include <fcntl.h>
#endif

#define MAX_HEADER 16384
#define MAX_BODY   (1024*1024)

/* ---------- helpers de resposta ---------------------------------- */

int http_respond_text(HttpResponse *res, int status, const char *text)
{
    if (!res) return ERROR_INVALID_INPUT;
    res->status = status;
    res->content_type = "text/plain; charset=utf-8";
    int n = snprintf(res->body_storage, sizeof res->body_storage, "%s", text ? text : "");
    if (n < 0 || (size_t)n >= sizeof res->body_storage) return ERROR_TOO_LONG;
    res->body = res->body_storage;
    res->body_len = (size_t)n;
    return SUCCESS;
}

int http_respond_json(HttpResponse *res, int status, const char *json)
{
    if (!res) return ERROR_INVALID_INPUT;
    res->status = status;
    res->content_type = "application/json; charset=utf-8";
    int n = snprintf(res->body_storage, sizeof res->body_storage, "%s", json ? json : "{}");
    if (n < 0 || (size_t)n >= sizeof res->body_storage) return ERROR_TOO_LONG;
    res->body = res->body_storage;
    res->body_len = (size_t)n;
    return SUCCESS;
}

int http_respond_error(HttpResponse *res, int code, const char *msg)
{
    if (!res) return ERROR_INVALID_INPUT;
    int n = snprintf(res->body_storage, sizeof res->body_storage,
        "{\"error\":%d,\"message\":\"%s\"}", code, msg ? msg : "");
    if (n < 0 || (size_t)n >= sizeof res->body_storage) return ERROR_TOO_LONG;
    res->status = code;
    res->content_type = "application/json; charset=utf-8";
    res->body = res->body_storage;
    res->body_len = (size_t)n;
    return SUCCESS;
}

/* ---------- parsing da request ----------------------------------- */

static const char *get_header_value(const char *raw_headers,
                                    const char *name, char *buf, size_t cap)
{
    if (!raw_headers) return NULL;
    size_t nlen = strlen(name);
    const char *p = raw_headers;
    while (*p) {
        if (strncasecmp(p, name, nlen) == 0 && p[nlen] == ':') {
            p += nlen + 1;
            while (*p == ' ' || *p == '\t') p++;
            const char *e = strstr(p, "\r\n");
            if (!e) e = p + strlen(p);
            size_t n = (size_t)(e - p);
            if (n >= cap) n = cap - 1;
            memcpy(buf, p, n);
            buf[n] = '\0';
            return buf;
        }
        const char *e = strstr(p, "\r\n");
        if (!e) break;
        p = e + 2;
    }
    return NULL;
}

static char *get_header_value_ptr(char *raw_headers, const char *name)
{
    size_t nlen = strlen(name);
    char *p = raw_headers;
    while (*p) {
        if (strncasecmp(p, name, nlen) == 0 && p[nlen] == ':') {
            p += nlen + 1;
            while (*p == ' ' || *p == '\t') p++;
            char *e = strstr(p, "\r\n");
            if (e) *e = '\0';
            return p;
        }
        char *e = strstr(p, "\r\n");
        if (!e) break;
        p = e + 2;
    }
    return NULL;
}

static int parse_request(int cfd, HttpRequest *out)
{
    char *buf = (char *)malloc(MAX_HEADER + MAX_BODY + 1);
    if (!buf) return -1;
    size_t off = 0;
    while (off < MAX_HEADER) {
        ssize_t n = recv(cfd, buf + off, MAX_HEADER - off, 0);
        if (n <= 0) { free(buf); return -1; }
        off += (size_t)n;
        buf[off] = '\0';
        if (strstr(buf, "\r\n\r\n")) break;
    }
    char *body = strstr(buf, "\r\n\r\n");
    if (!body) { free(buf); return -1; }
    *body = '\0';
    body += 4;

    /* Request line: METHOD SP PATH SP HTTP/1.x */
    char *line_end = strstr(buf, "\r\n");
    if (!line_end) { free(buf); return -1; }
    *line_end = '\0';

    char *method = buf, *path_qs = strchr(method, ' ');
    if (!path_qs) { free(buf); return -1; }
    *path_qs++ = '\0';
    char *ver = strchr(path_qs, ' ');
    if (!ver || strlen(method) >= 8 || strlen(path_qs) >= 1024) { free(buf); return -1; }
    *ver = '\0';
    (void)ver;

    char *qmark = strchr(path_qs, '?');
    if (qmark) { *qmark = '\0'; out->query = qmark + 1; }
    else out->query = "";

    out->method = method;
    out->path   = path_qs;
    out->body   = body;
    out->body_len = off - (size_t)(body - buf);

    char content_length[32] = {0};
    const char *cl = get_header_value(line_end + 2, "Content-Length", content_length, sizeof content_length);
    char *end = NULL;
    long expected = cl ? strtol(cl, &end, 10) : 0;
    if (expected < 0 || expected > MAX_BODY || (cl && (*end != '\0'))) { free(buf); return -1; }
    while (out->body_len < (size_t)expected) {
        ssize_t n = recv(cfd, body + out->body_len, (size_t)expected - out->body_len, 0);
        if (n <= 0) { free(buf); return -1; }
        out->body_len += (size_t)n;
    }
    body[out->body_len] = '\0';
    char *h = get_header_value_ptr(line_end + 2, "Authorization");
    out->auth = (h && strncasecmp(h, "Bearer ", 7) == 0) ? h + 7 : NULL;
    out->storage = buf;
    return 0;
}

static void send_response(int cfd, const HttpResponse *res)
{
    char head[512];
    int n = snprintf(head, sizeof head,
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Authorization, Content-Type\r\n"
        "\r\n",
        res->status,
        res->status == 200 ? "OK" :
        res->status == 201 ? "Created" :
        res->status == 204 ? "No Content" :
        res->status == 400 ? "Bad Request" :
        res->status == 401 ? "Unauthorized" :
        res->status == 403 ? "Forbidden" :
        res->status == 404 ? "Not Found" :
        res->status == 405 ? "Method Not Allowed" : "Error",
        res->content_type ? res->content_type : "application/json",
        res->body_len);
    (void)send(cfd, head, (size_t)n, 0);
    if (res->body && res->body_len) (void)send(cfd, res->body, res->body_len, 0);
}

/* ---------- dispatcher declarado externamente em router.c ------- */
struct RouterCtx;
int router_dispatch_request(const struct RouterCtx *ctx, const HttpRequest *req,
                            HttpResponse *res, char *id_out, size_t cap);

typedef struct {
    int    cfd;
    void  *server_ctx;
} ConnArg;

static void *client_thread(void *arg)
{
    ConnArg *c = (ConnArg *)arg;
    HttpRequest req = {0};
    HttpResponse res = {0};
    char id_buf[64] = {0};

    if (parse_request(c->cfd, &req) != 0) {
        http_respond_error(&res, 400, "bad request");
    } else if (strcmp(req.method, "OPTIONS") == 0) {
        http_respond_text(&res, 204, "");
    } else {
        log_info("%s %s body=%zu", req.method, req.path, req.body_len);
        /* router_dispatch_request é definido em router.c (mesma TU não dá,
         * por isso usamos extern via header). Implementação inline abaixo. */
        extern int router_dispatch_request_impl(void *server_ctx,
            const HttpRequest *req, HttpResponse *res, char *id_out, size_t cap);
        router_dispatch_request_impl(c->server_ctx, &req, &res, id_buf, sizeof id_buf);
    }
    send_response(c->cfd, &res);
    free(req.storage);
    close(c->cfd);
    free(c);
    return NULL;
}

#if defined(_WIN32)
static DWORD WINAPI client_thread_win(LPVOID arg)
{
    (void)client_thread(arg);
    return 0;
}
#endif

int http_server_run(const char *host, int port, void *ctx,
                    volatile int *should_stop)
{
#if defined(_WIN32)
    WSADATA wsad; WSAStartup(MAKEWORD(2,2), &wsad);
#else
    signal(SIGPIPE, SIG_IGN);
#endif

    int sfd = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) { log_error("socket: %s", strerror(errno)); return ERROR_NETWORK; }
    int opt = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof opt);

    struct sockaddr_in addr; memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    if (!host || !*host || strcmp(host, "0.0.0.0") == 0) addr.sin_addr.s_addr = htonl(INADDR_ANY);
    else inet_pton(AF_INET, host, &addr.sin_addr);

    if (bind(sfd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        log_error("bind: %s", strerror(errno)); close(sfd); return ERROR_NETWORK;
    }
    if (listen(sfd, 64) < 0) {
        log_error("listen: %s", strerror(errno)); close(sfd); return ERROR_NETWORK;
    }
    log_info("HTTP server escutando em %s:%d", host ? host : "0.0.0.0", port);

    while (!*should_stop) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(sfd, &readable);
        struct timeval timeout = { .tv_sec = 1, .tv_usec = 0 };
#if defined(_WIN32)
        int ready = select(0, &readable, NULL, NULL, &timeout);
#else
        int ready = select(sfd + 1, &readable, NULL, NULL, &timeout);
#endif
        if (ready == 0) continue;
        if (ready < 0) { if (*should_stop) break; continue; }
#if defined(_WIN32)
        SOCKET cfd = accept(sfd, NULL, NULL);
        if (cfd == INVALID_SOCKET) break;
#else
        int cfd = accept(sfd, NULL, NULL);
        if (cfd < 0) break;
#endif
        ConnArg *c = (ConnArg *)malloc(sizeof *c);
        c->cfd = cfd;
        c->server_ctx = ctx;
#if defined(_WIN32)
        /* Winsock não tem pthreads portáveis; criamos via _beginthreadex. */
        HANDLE th = CreateThread(NULL, 0,
            client_thread_win, c, 0, NULL);
        if (th) CloseHandle(th);
        else { closesocket(cfd); free(c); }
#else
        pthread_t t;
        if (pthread_create(&t, NULL, client_thread, c) != 0) {
            close(cfd); free(c);
        } else {
            pthread_detach(t);
        }
#endif
    }
    close(sfd);
#if defined(_WIN32)
    WSACleanup();
#endif
    return SUCCESS;
}
