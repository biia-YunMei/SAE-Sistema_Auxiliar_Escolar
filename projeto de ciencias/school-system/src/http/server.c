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
#include <stdatomic.h>
#include <errno.h>
#include <time.h>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  typedef SOCKET socket_t;
  typedef SSIZE_T ssize_t;
  #define close closesocket
  #define strncasecmp _strnicmp
#else
  #include <sys/socket.h>
  #include <sys/select.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <strings.h>
  #include <unistd.h>
  #include <pthread.h>
  #include <signal.h>
  #include <errno.h>
  #include <fcntl.h>
  typedef int socket_t;
#endif

#define MAX_HEADER 16384
#define MAX_BODY   (1024*1024)
#define MAX_CLIENTS 64
static atomic_uint active_clients;

/* ---------- helpers de resposta ---------------------------------- */

/* Mantém mensagens internas curtas nos controllers, mas entrega ao usuário
 * uma orientação clara e em português. O status HTTP continua disponível
 * para a interface identificar a categoria do erro. */
static const char *friendly_error_message(const char *message)
{
    static const struct {
        const char *internal;
        const char *friendly;
    } messages[] = {
        { "administrator already exists", "Já existe um administrador cadastrado." },
        { "bad request", "Não foi possível entender a solicitação enviada." },
        { "class exists", "Já existe uma turma com esse nome." },
        { "class_id and message are required", "Informe a turma e a mensagem." },
        { "class_id required", "Informe a turma." },
        { "cannot delete own user", "Você não pode excluir o próprio usuário enquanto está conectado." },
        { "coordinator only", "Esta ação é permitida somente para coordenadores." },
        { "could not assign class", "Não foi possível vincular o aluno à turma." },
        { "could not create administrator", "Não foi possível criar o administrador." },
        { "could not create notice", "Não foi possível publicar o aviso." },
        { "could not create class", "Não foi possível criar ou localizar essa turma." },
        { "could not link class", "Não foi possível liberar essa turma para o professor." },
        { "could not create session", "Não foi possível iniciar sua sessão. Tente novamente." },
        { "create failed", "Não foi possível concluir o cadastro. Verifique os dados e tente novamente." },
        { "database error", "Não foi possível consultar os dados agora. Tente novamente." },
        { "delete failed", "Não foi possível excluir o item. Tente novamente." },
        { "due_date required", "Informe a data de entrega." },
        { "forbidden", "Você não tem permissão para realizar esta ação." },
        { "grade required", "Informe a série da turma." },
        { "id required", "Informe a identificação do item." },
        { "id too long", "A identificação informada é muito longa." },
        { "ids required", "Informe as identificações necessárias." },
        { "invalid credentials", "Telefone ou senha incorretos. Confira os dados e tente novamente." },
        { "invalid password length", "A senha deve ter entre 8 e 127 caracteres." },
        { "invalid setup key", "A chave temporária de configuração está incorreta." },
        { "message id required", "A mensagem recebida não tem um identificador único." },
        { "bootstrap is disabled; configure SAE_BOOTSTRAP_KEY", "Configure SAE_BOOTSTRAP_KEY no ambiente do servidor antes do primeiro cadastro." },
        { "SAE_BOOTSTRAP_KEY must contain at least 32 characters", "A chave temporária deve ter pelo menos 32 caracteres." },
        { "invalid due_date", "A data de entrega não é válida. Use o formato AAAA-MM-DD." },
        { "invalid id", "A identificação informada não é válida." },
        { "invalid email", "Informe um endereço de e-mail válido." },
        { "invalid message", "A mensagem informada não é válida." },
        { "invalid phone", "O número de telefone informado não é válido." },
        { "invalid student data", "Confira o nome e a série do aluno." },
        { "invalid session", "Sua sessão expirou ou já foi encerrada. Faça login novamente." },
        { "invalid type", "O tipo de usuário informado não é válido." },
        { "invalid verification request", "A verificação recebida não é válida." },
        { "invalid webhook signature", "Não foi possível confirmar a origem da mensagem." },
        { "link failed", "Não foi possível vincular o professor à turma." },
        { "list failed", "Não foi possível carregar a lista agora. Tente novamente." },
        { "login failed", "Não foi possível entrar agora. Tente novamente em alguns instantes." },
        { "method not allowed", "Esta ação não está disponível nesta página." },
        { "missing authorization", "Sua sessão não foi encontrada. Faça login novamente para continuar." },
        { "missing fields", "Preencha todos os campos obrigatórios." },
        { "missing phone", "Informe o número de telefone para entrar." },
        { "missing webhook signature", "Não foi possível confirmar a origem da mensagem." },
        { "name required", "Informe o nome." },
        { "name and phone are required", "Informe o nome e o WhatsApp do professor." },
        { "name, phone and grade are required", "Informe o nome, telefone e série do aluno." },
        { "name, phone and password are required", "Informe nome, telefone e senha." },
        { "name, phone and class_id are required", "Informe nome, telefone e turma do aluno." },
        { "not found", "O item solicitado não foi encontrado." },
        { "not linked to class", "O professor não está vinculado a esta turma." },
        { "not owner", "Você não tem permissão para alterar esta tarefa." },
        { "notification delivery failed", "Não foi possível enviar a notificação." },
        { "oom", "O sistema ficou sem memória para concluir esta ação." },
        { "phone already registered", "Este telefone já está cadastrado." },
        { "phone and message are required", "Informe o telefone e a mensagem." },
        { "phone required", "Informe o número de telefone." },
        { "processing failed", "Não foi possível processar a mensagem." },
        { "query failed", "Não foi possível consultar os dados agora. Tente novamente." },
        { "route not found", "A página solicitada não foi encontrada." },
        { "signature verification failed", "Não foi possível confirmar a origem da mensagem." },
        { "signature verification unavailable", "A confirmação de segurança não está disponível agora." },
        { "student not found", "O aluno informado não foi encontrado." },
        { "use student registration", "Cadastre alunos pela aba Alunos, informando nome, telefone e série." },
        { "student tasks only", "Você só pode consultar as tarefas da sua própria turma." },
        { "subject required", "Informe a matéria." },
        { "teacher profile not found", "O cadastro de professor não foi encontrado." },
        { "teacher_id required", "Informe o professor responsável." },
        { "text too long", "O texto é maior do que o permitido." },
        { "title and content are required", "Informe o título e o conteúdo do aviso." },
        { "title required", "Informe o título." },
        { "too many login attempts; try again later", "Foram feitas muitas tentativas. Aguarde alguns minutos e tente novamente." },
        { "type required", "Informe o tipo de usuário." },
        { "unlink failed", "Não foi possível remover o vínculo do professor com a turma." },
        { "update failed", "Não foi possível salvar as alterações. Tente novamente." },
        { "user_id and class_id are required", "Informe o aluno e a turma." },
        { "verification failed", "Não foi possível confirmar a verificação." },
        { "webhook not configured", "A integração de mensagens ainda não está configurada." },
        { "webhook signature not configured", "Configure o segredo da aplicação para validar o webhook do WhatsApp." }
    };
    if (!message) return "Ocorreu um erro inesperado. Tente novamente.";
    for (size_t i = 0; i < sizeof messages / sizeof messages[0]; ++i)
        if (strcmp(message, messages[i].internal) == 0) return messages[i].friendly;
    return message;
}

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
    msg = friendly_error_message(msg);
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

static int header_present(const char *raw_headers, const char *name)
{
    char value[8];
    return get_header_value(raw_headers, name, value, sizeof value) != NULL;
}

static int header_count(const char *raw_headers, const char *name)
{
    if (!raw_headers || !name) return 0;
    size_t name_len = strlen(name);
    int count = 0;
    const char *line = raw_headers;
    while (*line) {
        const char *end = strstr(line, "\r\n");
        if (!end) break;
        if ((size_t)(end - line) > name_len &&
            strncasecmp(line, name, name_len) == 0 && line[name_len] == ':')
            ++count;
        line = end + 2;
    }
    return count;
}

static int valid_method_token(const char *method)
{
    if (!method || !*method) return 0;
    for (const unsigned char *p = (const unsigned char *)method; *p; ++p)
        if (!( (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
               (*p >= '0' && *p <= '9') || strchr("!#$%&'*+-.^_`|~", *p)))
            return 0;
    return 1;
}

static int parse_request(socket_t cfd, HttpRequest *out)
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
    if (!ver || strlen(method) >= 8 || strlen(path_qs) >= 1024 ||
        !valid_method_token(method)) { free(buf); return -1; }
    *ver = '\0';
    if (strcmp(ver + 1, "HTTP/1.1") != 0 && strcmp(ver + 1, "HTTP/1.0") != 0) {
        free(buf); return -1;
    }
    if (path_qs[0] != '/' || strchr(path_qs, '#')) { free(buf); return -1; }

    const char *headers = line_end + 2;
    if (header_count(headers, "Content-Length") > 1 ||
        header_present(headers, "Transfer-Encoding") ||
        (strcmp(ver + 1, "HTTP/1.1") == 0 && !header_present(headers, "Host"))) {
        free(buf); return -1;
    }

    char *qmark = strchr(path_qs, '?');
    if (qmark) { *qmark = '\0'; out->query = qmark + 1; }
    else out->query = "";

    out->method = method;
    out->path   = path_qs;
    out->body   = body;
    out->body_len = off - (size_t)(body - buf);

    char content_length[32] = {0};
    const char *cl = get_header_value(headers, "Content-Length", content_length, sizeof content_length);
    char *end = NULL;
    errno = 0;
    long expected = cl ? strtol(cl, &end, 10) : 0;
    if (errno == ERANGE || expected < 0 || expected > MAX_BODY ||
        (cl && (end == cl || *end != '\0')) || (!cl && out->body_len != 0) ||
        out->body_len > (size_t)expected) { free(buf); return -1; }
    while (out->body_len < (size_t)expected) {
        ssize_t n = recv(cfd, body + out->body_len, (size_t)expected - out->body_len, 0);
        if (n <= 0) { free(buf); return -1; }
        out->body_len += (size_t)n;
    }
    if (out->body_len && memchr(body, '\0', out->body_len)) { free(buf); return -1; }
    body[out->body_len] = '\0';
    const char *authorization = get_header_value(headers, "Authorization",
                                                  out->auth_storage,
                                                  sizeof out->auth_storage);
    out->auth = (authorization && strncasecmp(authorization, "Bearer ", 7) == 0)
        ? authorization + 7 : NULL;
    out->webhook_signature = get_header_value(headers, "X-Hub-Signature-256",
                                               out->webhook_signature_storage,
                                               sizeof out->webhook_signature_storage);
    out->storage = buf;
    return 0;
}

static void send_response(socket_t cfd, const HttpResponse *res)
{
    char head[512];
    int n = snprintf(head, sizeof head,
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "X-Frame-Options: DENY\r\n"
        "Referrer-Policy: no-referrer\r\n"
        "Cache-Control: no-store\r\n"
        "\r\n",
        res->status,
        res->status == 200 ? "OK" :
        res->status == 201 ? "Created" :
        res->status == 204 ? "No Content" :
        res->status == 400 ? "Bad Request" :
        res->status == 401 ? "Unauthorized" :
        res->status == 403 ? "Forbidden" :
        res->status == 404 ? "Not Found" :
        res->status == 405 ? "Method Not Allowed" :
        res->status == 429 ? "Too Many Requests" :
        res->status == 501 ? "Not Implemented" : "Error",
        res->content_type ? res->content_type : "application/json",
        res->body_len);
    size_t sent = 0;
    while (sent < (size_t)n) {
        ssize_t part = send(cfd, head + sent, (size_t)n - sent, 0);
        if (part <= 0) return;
        sent += (size_t)part;
    }
    sent = 0;
    while (res->body && sent < res->body_len) {
        ssize_t part = send(cfd, res->body + sent, res->body_len - sent, 0);
        if (part <= 0) return;
        sent += (size_t)part;
    }
}

/* ---------- dispatcher declarado externamente em router.c ------- */
struct RouterCtx;
int router_dispatch_request(const struct RouterCtx *ctx, const HttpRequest *req,
                            HttpResponse *res, char *id_out, size_t cap);

typedef struct {
    socket_t cfd;
    void  *server_ctx;
    char client_ip[64];
} ConnArg;

static void *client_thread(void *arg)
{
    ConnArg *c = (ConnArg *)arg;
    HttpRequest req = {0};
    HttpResponse res = {0};
    char id_buf[64] = {0};
    snprintf(req.client_ip_storage, sizeof req.client_ip_storage, "%s", c->client_ip);
    req.client_ip = req.client_ip_storage;

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
    atomic_fetch_sub_explicit(&active_clients, 1, memory_order_release);
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
    if (!should_stop || port <= 0 || port > 65535) return ERROR_INVALID_INPUT;
#if defined(_WIN32)
    WSADATA wsad; WSAStartup(MAKEWORD(2,2), &wsad);
#else
    signal(SIGPIPE, SIG_IGN);
#endif

    socket_t sfd = socket(AF_INET, SOCK_STREAM, 0);
#if defined(_WIN32)
    if (sfd == INVALID_SOCKET) { log_error("socket falhou"); return ERROR_NETWORK; }
#else
    if (sfd < 0) { log_error("socket: %s", strerror(errno)); return ERROR_NETWORK; }
#endif
    int opt = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof opt);

    struct sockaddr_in addr; memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    if (!host || !*host || strcmp(host, "0.0.0.0") == 0) addr.sin_addr.s_addr = htonl(INADDR_ANY);
    else if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        log_error("endereço SERVER_HOST inválido"); close(sfd); return ERROR_CONFIG;
    }
    if ((ntohl(addr.sin_addr.s_addr) >> 24) != 127) {
        log_error("bind remoto recusado: o servidor não oferece TLS; use um proxy HTTPS e mantenha SERVER_HOST=127.0.0.1");
        close(sfd);
        return ERROR_CONFIG;
    }

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
        socket_t cfd = accept(sfd, NULL, NULL);
        if (cfd == INVALID_SOCKET) break;
#else
        socket_t cfd = accept(sfd, NULL, NULL);
        if (cfd < 0) break;
#endif
        unsigned int active = atomic_load_explicit(&active_clients, memory_order_relaxed);
        do {
            if (active >= MAX_CLIENTS) { close(cfd); cfd = (socket_t)-1; break; }
        } while (!atomic_compare_exchange_weak_explicit(&active_clients, &active, active + 1,
                    memory_order_acquire, memory_order_relaxed));
        if (cfd == (socket_t)-1) continue;
        /* Limita conexões lentas que manteriam uma thread presa indefinidamente. */
        struct timeval io_timeout = { .tv_sec = 10, .tv_usec = 0 };
        setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&io_timeout, sizeof io_timeout);
        setsockopt(cfd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&io_timeout, sizeof io_timeout);
        ConnArg *c = (ConnArg *)malloc(sizeof *c);
        if (!c) {
            close(cfd);
            atomic_fetch_sub_explicit(&active_clients, 1, memory_order_release);
            continue;
        }
        c->cfd = cfd;
        c->server_ctx = ctx;
        struct sockaddr_in peer_addr;
        memset(&peer_addr, 0, sizeof peer_addr);
#if defined(_WIN32)
        int peer_len = (int)sizeof peer_addr;
#else
        socklen_t peer_len = (socklen_t)sizeof peer_addr;
#endif
        if (getpeername(cfd, (struct sockaddr *)&peer_addr, &peer_len) == 0)
            (void)inet_ntop(AF_INET, &peer_addr.sin_addr, c->client_ip, sizeof c->client_ip);
        if (!c->client_ip[0]) snprintf(c->client_ip, sizeof c->client_ip, "unknown");
#if defined(_WIN32)
        /* Winsock não tem pthreads portáveis; criamos via _beginthreadex. */
        HANDLE th = CreateThread(NULL, 0,
            client_thread_win, c, 0, NULL);
        if (th) CloseHandle(th);
        else { closesocket(cfd); free(c); atomic_fetch_sub(&active_clients, 1); }
#else
        pthread_t t;
        if (pthread_create(&t, NULL, client_thread, c) != 0) {
            close(cfd); free(c); atomic_fetch_sub(&active_clients, 1);
        } else {
            pthread_detach(t);
        }
#endif
    }
    close(sfd);
    /* As threads são destacadas; aguarda as requisições em andamento antes
     * de o chamador encerrar banco, logger e demais dependências globais. */
    while (atomic_load_explicit(&active_clients, memory_order_acquire) != 0) {
#if defined(_WIN32)
        Sleep(50);
#else
        struct timespec pause = { .tv_sec = 0, .tv_nsec = 50000000L };
        (void)nanosleep(&pause, NULL);
#endif
    }
#if defined(_WIN32)
    WSACleanup();
#endif
    return SUCCESS;
}
