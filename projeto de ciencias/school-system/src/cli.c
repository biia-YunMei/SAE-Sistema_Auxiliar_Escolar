#include "cli.h"
#include "error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#define close_socket closesocket
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#define close_socket close
#endif

#define SESSION_FILE ".sae-session"

static void help(void)
{
    printf("Sae - Sistema Auxiliar Escolar: gestao de tarefas para professores e alunos\n\n");
    printf("Primeiro inicie o servidor em um terminal:\n  sae servidor\n\n");
    printf("Comandos simples (em outro terminal):\n");
    printf("  sae -ca -n NOME -p TELEFONE -s SENHA      Cria o primeiro administrador.\n");
    printf("  sae login -p TELEFONE -s SENHA             Entra e salva sua sessao.\n");
    printf("  sae usuario criar -n NOME -p TELEFONE -t TIPO [-s SENHA]\n");
    printf("       Tipos: COORD, PROF ou STUD.\n");
    printf("  sae usuario listar                         Lista os usuarios.\n");
    printf("  sae turma criar -n NOME -g SERIE [-t TURNO] Cria turma (MANHA, TARDE, NOITE, INTEGRAL).\n");
    printf("  sae turma listar                           Lista turmas.\n");
    printf("  sae turma vincular -c ID_TURMA -u ID_PROF  Vincula professor a turma.\n");
    printf("  sae tarefa criar -c TURMA -u PROF -m MATERIA -n TITULO -v AAAA-MM-DD [-d DESCRICAO]\n");
    printf("  sae tarefa listar -c ID_TURMA               Lista tarefas da turma.\n");
    printf("  sae --help                                  Mostra esta ajuda.\n\n");
    printf("Opcoes curtas: -n nome | -p telefone | -s senha | -t tipo/turno | -c turma | -u professor | -m materia | -v vencimento | -d descricao.\n");
}

static const char *opt(int argc, char **argv, const char *key)
{
    for (int i = 1; i + 1 < argc; i++) if (strcmp(argv[i], key) == 0) return argv[i + 1];
    return NULL;
}

static int json_text(const char *s, char *out, size_t cap)
{
    size_t j = 0;
    if (!s) s = "";
    for (size_t i = 0; s[i]; i++) {
        if (s[i] == '"' || s[i] == '\\' || s[i] == '\n' || s[i] == '\r') return 0;
        if (j + 1 >= cap) return 0;
        out[j++] = s[i];
    }
    out[j] = '\0'; return 1;
}

static int token_load(char *token, size_t cap)
{
    FILE *f = fopen(SESSION_FILE, "r");
    if (!f) return 0;
    int ok = fgets(token, (int)cap, f) != NULL;
    fclose(f);
    token[strcspn(token, "\r\n")] = '\0';
    return ok && token[0];
}

static int request(const char *method, const char *path, const char *body, char *response, size_t cap)
{
#if defined(_WIN32)
    WSADATA w; if (WSAStartup(MAKEWORD(2, 2), &w) != 0) return 0;
#endif
    int s = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return 0;
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    if (connect(s, (struct sockaddr *)&a, sizeof a) < 0) { close_socket(s); return 0; }
    char token[80] = {0}, head[2048];
    token_load(token, sizeof token);
    int n = snprintf(head, sizeof head,
        "%s %s HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n%s%s%sConnection: close\r\n\r\n%s",
        method, path, strlen(body ? body : ""), token[0] ? "Authorization: Bearer " : "",
        token[0] ? token : "", token[0] ? "\r\n" : "", body ? body : "");
    if (n < 0 || (size_t)n >= sizeof head || send(s, head, n, 0) != n) { close_socket(s); return 0; }
    size_t used = 0;
    while (used + 1 < cap) { int r = (int)recv(s, response + used, cap - used - 1, 0); if (r <= 0) break; used += (size_t)r; }
    response[used] = '\0'; close_socket(s);
#if defined(_WIN32)
    WSACleanup();
#endif
    char *payload = strstr(response, "\r\n\r\n");
    if (payload) memmove(response, payload + 4, strlen(payload + 4) + 1);
    return strstr(response - 0, "") != NULL;
}

static int call(const char *method, const char *path, const char *body)
{
    char response[65536];
    if (!request(method, path, body, response, sizeof response)) {
        fprintf(stderr, "Nao foi possivel falar com o servidor. Inicie com: sae servidor\n"); return ERROR_NETWORK;
    }
    printf("%s\n", response);
    return SUCCESS;
}

int cli_run(int argc, char **argv)
{
    if (argc < 2) { help(); return SUCCESS; }
    if (strcmp(argv[1], "servidor") == 0) return -1;
    if (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h") || !strcmp(argv[1], "ajuda")) { help(); return SUCCESS; }
    const char *n = opt(argc, argv, "-n"), *p = opt(argc, argv, "-p"), *s = opt(argc, argv, "-s");
    const char *t = opt(argc, argv, "-t"), *c = opt(argc, argv, "-c"), *u = opt(argc, argv, "-u");
    const char *m = opt(argc, argv, "-m"), *v = opt(argc, argv, "-v"), *d = opt(argc, argv, "-d"), *g = opt(argc, argv, "-g");
    char a[128], b[128], secret[128], q[4096];
    if ((strcmp(argv[1], "-ca") == 0 || strcmp(argv[1], "--create-admin") == 0) && n && p && s) {
        if (!json_text(n,a,sizeof a)||!json_text(p,b,sizeof b)||!json_text(s,secret,sizeof secret)) return ERROR_INVALID_INPUT;
        snprintf(q,sizeof q,"{\"name\":\"%s\",\"phone\":\"%s\",\"password\":\"%s\"}",a,b,secret); return call("POST","/api/bootstrap",q);
    }
    if (!strcmp(argv[1], "login") && p && s) {
        if (!json_text(p,a,sizeof a)||!json_text(s,b,sizeof b)) return ERROR_INVALID_INPUT;
        snprintf(q,sizeof q,"{\"phone\":\"%s\",\"password\":\"%s\"}",a,b);
        char r[4096]; if (!request("POST","/api/auth",q,r,sizeof r)) return ERROR_NETWORK;
        char *x=strstr(r,"\"token\":\""); if (!x) { printf("%s\n",r); return ERROR_UNAUTHORIZED; }
        x += 9; char *end=strchr(x,'"'); if (!end) return ERROR_HTTP; *end='\0'; FILE *f=fopen(SESSION_FILE,"w"); if (!f) return ERROR_INTERNAL; fprintf(f,"%s\n",x); fclose(f); printf("Login realizado. Sessao salva.\n"); return SUCCESS;
    }
    if (!strcmp(argv[1], "usuario") && argc > 2 && !strcmp(argv[2], "listar")) return call("GET","/api/users","");
    if (!strcmp(argv[1], "usuario") && argc > 2 && !strcmp(argv[2], "criar") && n && p && t) { snprintf(q,sizeof q,"{\"name\":\"%s\",\"phone\":\"%s\",\"type\":\"%s\",\"password\":\"%s\"}",n,p,t,s?s:""); return call("POST","/api/users",q); }
    if (!strcmp(argv[1], "turma") && argc > 2 && !strcmp(argv[2], "listar")) return call("GET","/api/classes","");
    if (!strcmp(argv[1], "turma") && argc > 2 && !strcmp(argv[2], "criar") && n && g) {
        const char *turn = t ? t : "MORNING";
        if (!strcmp(turn, "MANHA")) turn = "MORNING";
        else if (!strcmp(turn, "TARDE")) turn = "AFTERNOON";
        else if (!strcmp(turn, "NOITE")) turn = "EVENING";
        else if (!strcmp(turn, "INTEGRAL")) turn = "FULL";
        snprintf(q,sizeof q,"{\"name\":\"%s\",\"grade\":\"%s\",\"shift\":\"%s\"}",n,g,turn); return call("POST","/api/classes",q);
    }
    if (!strcmp(argv[1], "turma") && argc > 2 && !strcmp(argv[2], "vincular") && c && u) { snprintf(q,sizeof q,"{\"teacher_id\":%s}",u); snprintf(a,sizeof a,"/api/classes/%s/teachers",c); return call("POST",a,q); }
    if (!strcmp(argv[1], "tarefa") && argc > 2 && !strcmp(argv[2], "listar") && c) { snprintf(a,sizeof a,"/api/tasks?class_id=%s",c); return call("GET",a,""); }
    if (!strcmp(argv[1], "tarefa") && argc > 2 && !strcmp(argv[2], "criar") && c && u && m && n && v) { snprintf(q,sizeof q,"{\"class_id\":%s,\"teacher_id\":%s,\"subject\":\"%s\",\"title\":\"%s\",\"description\":\"%s\",\"due_date\":\"%s\"}",c,u,m,n,d?d:"",v); return call("POST","/api/tasks",q); }
    fprintf(stderr,"Comando incompleto ou desconhecido. Use: sae --help\n"); return ERROR_INVALID_INPUT;
}
