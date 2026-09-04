/* test_http.c
 * Sobe o servidor HTTP numa porta alta e faz um GET /api/health.
 *
 * Requer que o binário do servidor tenha sido compilado; aqui
 * conectamos direto via socket e validamos a resposta.
 */
#include "http_server.h"
#include "router.h"
#include "controllers.h"
#include "logger.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #define close closesocket
  #define sleep(x) Sleep((x)*1000)
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
#endif

static int passed = 0, failed = 0;
#define CHECK(expr) do { \
    if (expr) { passed++; } \
    else { failed++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #expr); } \
} while (0)

static const Route g_routes[] = {
    { "GET",  "/api/health", ctrl_health },
};
struct SR { const Route *routes; size_t n; };

static volatile int g_stop = 0;

static void *server_thread(void *arg)
{
    struct SR *sr = (struct SR *)arg;
    http_server_run("127.0.0.1", 18080, sr, &g_stop);
    return NULL;
}

static void http_get(int port, const char *path, char *buf, size_t cap)
{
    int s = (int)socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    if (connect(s, (struct sockaddr *)&a, sizeof a) < 0) { close(s); return; }
    char req[1024];
    int n = snprintf(req, sizeof req,
        "GET %s HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n", path);
    (void)send(s, req, (size_t)n, 0);

    size_t off = 0;
    for (;;) {
        int r = (int)recv(s, buf + off, cap - off - 1, 0);
        if (r <= 0) break;
        off += (size_t)r;
        if (off + 1 >= cap) break;
    }
    buf[off] = '\0';
    close(s);
}

int main(void)
{
    logger_init("logs", LOG_ERROR);
#if defined(_WIN32)
    WSADATA w; WSAStartup(MAKEWORD(2,2), &w);
#endif
    struct SR sr = { g_routes, sizeof g_routes / sizeof g_routes[0] };

    pthread_t th;
    pthread_create(&th, NULL, server_thread, &sr);
    sleep(1);

    char buf[4096];
    http_get(18080, "/api/health", buf, sizeof buf);
    CHECK(strstr(buf, "200 OK") != NULL);
    CHECK(strstr(buf, "\"status\":\"ok\"") != NULL);

    /* 404: rota inexistente deve retornar 404 */
    http_get(18080, "/api/nope", buf, sizeof buf);
    CHECK(strstr(buf, "404") != NULL);

    g_stop = 1;
    pthread_join(th, NULL);
#if defined(_WIN32)
    WSACleanup();
#endif
    printf("\nHTTP: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
