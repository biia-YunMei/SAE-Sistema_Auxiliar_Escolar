#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "cli.h"
#include "error.h"
#include "escola_api.h"
#include "ai.h"
#include "config.h"
#include "validation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <locale.h>
#include <ctype.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#include <fcntl.h>
#include <conio.h>
#include <windows.h>
#define close_socket closesocket
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#define close_socket close
#endif

#define SESSION_FILE ".sae-session"

/* A fonte escolhida pelo emulador de terminal não é exposta de forma
 * portável. Em vez de supor que os caracteres Unicode renderizam bem,
 * ativamos o formato visual apenas em terminais interativos UTF-8. O usuário
 * pode sempre escolher explicitamente SAE_UI=unicode ou SAE_UI=plain. */
static int unicode_ui_enabled(void)
{
    const char *choice = getenv("SAE_UI");
    if (choice && strcmp(choice, "plain") == 0) return 0;
    if (choice && (strcmp(choice, "unicode") == 0 || strcmp(choice, "nerd") == 0)) return 1;
#if defined(_WIN32)
    return _isatty(_fileno(stdout)) && GetConsoleOutputCP() == 65001;
#else
    if (!isatty(STDOUT_FILENO)) return 0;
    const char *locale = getenv("LC_ALL");
    if (!locale || !*locale) locale = getenv("LC_CTYPE");
    if (!locale || !*locale) locale = getenv("LANG");
    if (!locale || !*locale) locale = setlocale(LC_CTYPE, NULL);
    return locale && (strstr(locale, "UTF-8") || strstr(locale, "utf8") || strstr(locale, "UTF8"));
#endif
}

static void help_plain(void)
{
    puts("SAE — Sistema Auxiliar Escolar\n");
    puts("INÍCIO");
    puts("  sae servidor");
    puts("  sae -ca -n NOME -p TELEFONE (senha solicitada sem eco)");
    puts("  sae login -p TELEFONE | sae logout (senha solicitada sem eco)");
    puts("USUÁRIOS (COORD)");
    puts("  sae usuario criar|listar|editar|excluir");
    puts("  sae aluno vincular -u ID_ALUNO -c ID_TURMA");
    puts("TURMAS (COORD e PROF)");
    puts("  sae turma criar|listar|editar|excluir|vincular");
    puts("TAREFAS (COORD e PROF)");
    puts("  sae tarefa criar|listar|editar|excluir");
    puts("MENSAGENS (API escolar externa: somente resposta a mensagem recebida)");
    puts("  sae whatsapp enviar -i ID_DA_INBOX -m MENSAGEM");
    puts("  sae whatsapp turma -c ID_TURMA -m MENSAGEM (indisponível nesta API)");
    puts("API ESCOLAR (somente whoami, inbox, reply e ref)");
    puts("  sae escola whoami|inbox [-l LIMITE]|reply -i ID -m TEXTO|tutor -i ID -c TURMA -m PERGUNTA|ref");
    puts("Use SAE_UI=unicode para forçar o visual com molduras.");
}

static void help(void)
{
    if (!unicode_ui_enabled()) { help_plain(); return; }
    puts("╭──────────────────── SAE — SISTEMA AUXILIAR ESCOLAR ────────────────────────────╮");
    puts("│                                                                                │");
    puts("├────────────────────────────────── INICIO ──────────────────────────────────────┤");
    puts("│     sae servidor                                  Inicia o servidor.           │");
    puts("│     sae -ca -n NOME -p TELEFONE                   Cria o 1º coordenador.       │");
    puts("│     sae login -p TELEFONE                         Entra no sistema.            │");
    puts("│     sae logout                                    Encerra a sessão local.      │");
    puts("│                                                                                │");
    puts("├ USUÁRIOS (somente COORD para criar, editar e excluir) ─────────────────────────┤");
    puts("│     sae usuario criar -n NOME -p TELEFONE -t TIPO (senha solicitada)           │");
    puts("│          Tipos: COORD ou PROF. Aluno é cadastrado pela área da coordenação.    │");
    puts("│     sae usuario listar                                                         │");
    puts("│     sae usuario editar -u ID -n NOME -p TELEFONE [--senha]                     │");
    puts("│     sae usuario excluir -u ID                                                  │");
    puts("│     sae aluno vincular -u ID_ALUNO -c ID_TURMA                                 │");
    puts("│                                                                                │");
    puts("├─────────────────────── TURMAS (COORD e PROF) ──────────────────────────────────┤");
    puts("│     sae turma criar -n NOME -g SERIE [-t TURNO]                                │");
    puts("│          Turnos: MANHA, TARDE, NOITE ou INTEGRAL                               │");
    puts("│     sae turma listar                                                           │");
    puts("│     sae turma editar -i ID -n NOME -g SERIE -t TURNO                           │");
    puts("│     sae turma excluir -i ID                                                    │");
    puts("│     sae turma vincular -c ID_TURMA -u ID_USUARIO_PROF   (somente COORD)        │");
    puts("│                                                                                │");
    puts("├────────────────────── TAREFAS (COORD e PROF) ──────────────────────────────────┤");
    puts("│  sae tarefa criar -c TURMA -m MATERIA -n TITULO                                │");
    puts("│                       -v AAAA-MM-DD [-d DESCRICAO]                             │");
    puts("│                                                                                │");
    puts("│     COORD também informa: -u ID_PROF.                                          │");
    puts("│     sae tarefa listar -c ID_TURMA                                              │");
    puts("│     sae tarefa editar -i ID -m MATERIA -n TITU                                 │");
    puts("│                       -v AAAA-MM-DD [-d DESCRICAO]                             │");
    puts("│                                                                                │");
    puts("│     sae tarefa excluir -i ID                                                   │");
    puts("│     Alunos podem apenas listar tarefas da própria turma.                       │");
    puts("│                                                                                │");
    puts("├─────────────── MENSAGENS (API ESCOLAR EXTERNA) ────────────────────────────────┤");
    puts("│     sae whatsapp enviar -i ID_DA_INBOX -m MENSAGEM                             │");
    puts("│     Responde uma mensagem recebida; o ID tem formato m_...                     │");
    puts("│     sae whatsapp turma não está disponível: a API não oferece envio em massa.  │");
    puts("│                                                                                │");
    puts("│                                                                                │");
    puts("├────────────────── API ESCOLAR (whoami, inbox, reply e ref) ────────────────────┤");
    puts("│     sae escola whoami                                                          │");
    puts("│     sae escola inbox [-l LIMITE]                                               │");
    puts("│     sae escola reply -i ID_DA_INBOX -m MENSAGEM                                │");
    puts("│     sae escola tutor -i ID -c ID_TURMA -m PERGUNTA                             │");
    puts("│          IA tutora consulta tarefas da turma e responde ao ID da inbox.        │");
    puts("│     sae escola ref                                                             │");
    puts("│                                                                                │");
    puts("│                                                                                │");
    puts("├─────────────────────────────── OPÇÕES ─────────────────────────────────────────┤");
    puts("│                                                                                │");
    puts("│     -i identificação | -n nome/título | -p telefone | -s senha                 │");
    puts("│     -t tipo/turno | -c turma | -u usuário/professor | -m matéria/mensagem      │");
    puts("│     -g série | -v vencimento | -d descrição                                    │");
    puts("│     sae --help                                       Mostra esta ajuda.        │");
    puts("╰────────────────────────────────────────────────────────────────────────────────╯");
}

static const char *opt(int argc, char **argv, const char *key)
{
    for (int i = 1; i + 1 < argc; i++) if (strcmp(argv[i], key) == 0) return argv[i + 1];
    return NULL;
}

static int has_flag(int argc, char **argv, const char *flag)
{
    for (int i = 1; i < argc; ++i)
        if (strcmp(argv[i], flag) == 0) return 1;
    return 0;
}

static int json_text(const char *s, char *out, size_t cap)
{
    size_t j = 0;
    if (!s) s = "";
    for (size_t i = 0; s[i]; i++) {
        if (s[i] == '"' || s[i] == '\\' || (unsigned char)s[i] < 0x20) return 0;
        if (j + 1 >= cap) return 0;
        out[j++] = s[i];
    }
    out[j] = '\0'; return 1;
}

static int prompt_password(const char *prompt, char *out, size_t cap)
{
    if (!out || cap < 2) return 0;
    out[0] = '\0';
#if defined(_WIN32)
    fputs(prompt, stderr); fflush(stderr);
    size_t used = 0;
    for (;;) {
        int ch = _getch();
        if (ch == '\r' || ch == '\n') break;
        if (ch == 3 || ch == EOF) { memset(out, 0, cap); return 0; }
        if (ch == '\b') { if (used) --used; continue; }
        if (ch < 0x20 || used + 1 >= cap) { memset(out, 0, cap); return 0; }
        out[used++] = (char)ch;
    }
    out[used] = '\0';
    fputc('\n', stderr);
    return used > 0;
#else
    if (!isatty(STDIN_FILENO)) return 0;
    struct termios original, hidden;
    if (tcgetattr(STDIN_FILENO, &original) != 0) return 0;
    hidden = original;
    hidden.c_lflag &= (tcflag_t)~ECHO;
    fputs(prompt, stderr); fflush(stderr);
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden) != 0) return 0;
    char *result = fgets(out, (int)cap, stdin);
    int too_long = 0;
    if (result && !strchr(out, '\n')) {
        int trailing = fgetc(stdin);
        if (trailing != '\n' && trailing != EOF) {
            too_long = 1;
            while (trailing != '\n' && trailing != EOF) trailing = fgetc(stdin);
        }
    }
    int restore_ok = tcsetattr(STDIN_FILENO, TCSAFLUSH, &original) == 0;
    fputc('\n', stderr);
    if (!result || !restore_ok || too_long) {
        memset(out, 0, cap);
        return 0;
    }
    out[strcspn(out, "\r\n")] = '\0';
    return out[0] != '\0';
#endif
}

static void clear_secret(char *secret, size_t cap)
{
    volatile char *p = (volatile char *)secret;
    while (cap--) *p++ = '\0';
}

static int token_load(char *token, size_t cap)
{
    if (!token || cap < 65) return 0;
    token[0] = '\0';
#if defined(_WIN32)
    HANDLE handle = CreateFileA(SESSION_FILE, GENERIC_READ, FILE_SHARE_READ,
        NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (handle == INVALID_HANDLE_VALUE) return 0;
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(handle, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
        CloseHandle(handle);
        return 0;
    }
    int fd = _open_osfhandle((intptr_t)handle, _O_RDONLY | _O_BINARY);
    if (fd < 0) { CloseHandle(handle); return 0; }
    FILE *f = _fdopen(fd, "rb");
    if (!f) { _close(fd); return 0; }
#else
    int fd = open(SESSION_FILE, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return 0;
    struct stat file_stat;
    if (fstat(fd, &file_stat) != 0 || !S_ISREG(file_stat.st_mode) ||
        file_stat.st_uid != geteuid() || fchmod(fd, 0600) != 0) {
        close(fd);
        return 0;
    }
    FILE *f = fdopen(fd, "r");
    if (!f) { close(fd); return 0; }
#endif
    if (!f) return 0;
    int ok = fgets(token, (int)cap, f) != NULL;
    int trailing = fgetc(f);
    if (trailing != EOF || ferror(f)) ok = 0;
    fclose(f);
    token[strcspn(token, "\r\n")] = '\0';
    if (!ok || strlen(token) != 64) return 0;
    for (size_t i = 0; i < 64; ++i)
        if (!isxdigit((unsigned char)token[i])) return 0;
    return 1;
}

static int token_save_secure(const char *token)
{
    if (!token || strlen(token) != 64) return 0;
    for (size_t i = 0; i < 64; ++i)
        if (!isxdigit((unsigned char)token[i])) return 0;
#if defined(_WIN32)
    char temporary[MAX_PATH];
    int n = snprintf(temporary, sizeof temporary, ".sae-session.tmp.%lu",
                     (unsigned long)GetCurrentProcessId());
    if (n < 0 || (size_t)n >= sizeof temporary) return 0;
    HANDLE handle = CreateFileA(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) return 0;
    char line[66];
    memcpy(line, token, 64); line[64] = '\n'; line[65] = '\0';
    DWORD written = 0;
    int ok = WriteFile(handle, line, 65, &written, NULL) && written == 65 &&
             FlushFileBuffers(handle);
    if (!CloseHandle(handle)) ok = 0;
    if (ok && MoveFileExA(temporary, SESSION_FILE,
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return 1;
    (void)DeleteFileA(temporary);
    return 0;
#else
    char temporary[96];
    int n = snprintf(temporary, sizeof temporary, ".sae-session.tmp.%ld", (long)getpid());
    if (n < 0 || (size_t)n >= sizeof temporary) return 0;
    int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return 0;
    FILE *file = fdopen(fd, "w");
    if (!file) { close(fd); (void)unlink(temporary); return 0; }
    int ok = fprintf(file, "%s\n", token) > 0 && fflush(file) == 0 && fsync(fd) == 0;
    if (fclose(file) != 0) ok = 0;
    if (ok && rename(temporary, SESSION_FILE) == 0) return 1;
    (void)unlink(temporary);
    return 0;
#endif
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
    size_t body_len = strlen(body ? body : "");
    int n = snprintf(head, sizeof head,
        "%s %s HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n%s%s%sConnection: close\r\n\r\n",
        method, path, body_len, token[0] ? "Authorization: Bearer " : "",
        token[0] ? token : "", token[0] ? "\r\n" : "");
    if (n < 0 || (size_t)n >= sizeof head) { close_socket(s); return 0; }
    size_t sent = 0;
    while (sent < (size_t)n) {
        int chunk = (int)((size_t)n - sent);
        int written = (int)send(s, head + sent, chunk, 0);
        if (written <= 0) { close_socket(s); return 0; }
        sent += (size_t)written;
    }
    const char *request_body = body ? body : "";
    sent = 0;
    while (sent < body_len) {
        size_t remaining = body_len - sent;
        int chunk = remaining > 16384 ? 16384 : (int)remaining;
        int written = (int)send(s, request_body + sent, chunk, 0);
        if (written <= 0) { close_socket(s); return 0; }
        sent += (size_t)written;
    }
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

static int json_value(const char *object, const char *key, char *out, size_t cap)
{
    char needle[64];
    int written = snprintf(needle, sizeof needle, "\"%s\":\"", key);
    const char *value = written > 0 ? strstr(object, needle) : NULL;
    if (value) {
        value += written;
        const char *end = strchr(value, '"');
        if (!end || (size_t)(end - value) >= cap) return 0;
        memcpy(out, value, (size_t)(end - value));
        out[end - value] = '\0';
        return 1;
    }
    written = snprintf(needle, sizeof needle, "\"%s\":", key);
    value = written > 0 ? strstr(object, needle) : NULL;
    if (!value) return 0;
    value += written;
    const char *end = strpbrk(value, ",}");
    if (!end || (size_t)(end - value) >= cap) return 0;
    memcpy(out, value, (size_t)(end - value));
    out[end - value] = '\0';
    return 1;
}

static const char *shift_pt(const char *shift)
{
    if (strcmp(shift, "MORNING") == 0) return "Manhã";
    if (strcmp(shift, "AFTERNOON") == 0) return "Tarde";
    if (strcmp(shift, "EVENING") == 0) return "Noite";
    if (strcmp(shift, "FULL") == 0) return "Integral";
    return shift;
}

static const char *user_type_pt(const char *type)
{
    if (strcmp(type, "COORD") == 0) return "Coordenador";
    if (strcmp(type, "PROF") == 0) return "Professor";
    if (strcmp(type, "STUD") == 0) return "Aluno";
    return type;
}

static const char *status_pt(const char *status)
{
    if (strcmp(status, "ACTIVE") == 0) return "Ativo";
    if (strcmp(status, "INACTIVE") == 0) return "Inativo";
    if (strcmp(status, "ARCHIVED") == 0) return "Arquivada";
    return status;
}

static const char *turn_to_api(const char *turn)
{
    if (!turn || !*turn || strcmp(turn, "MANHA") == 0) return "MORNING";
    if (strcmp(turn, "TARDE") == 0) return "AFTERNOON";
    if (strcmp(turn, "NOITE") == 0) return "EVENING";
    if (strcmp(turn, "INTEGRAL") == 0) return "FULL";
    return turn;
}

#define BOX_INNER_WIDTH 77

/* Em UTF-8, á/ç usam mais de um byte, mas ocupam uma única coluna visual. */
static size_t utf8_prefix(const char *text, size_t limit, size_t *bytes)
{
    size_t columns = 0, used = 0;
    while (text[used] && columns < limit) {
        unsigned char ch = (unsigned char)text[used];
        size_t sequence = ch < 0x80 ? 1 : (ch < 0xE0 ? 2 : (ch < 0xF0 ? 3 : 4));
        size_t i;
        for (i = 1; i < sequence && text[used + i] &&
             (((unsigned char)text[used + i] & 0xC0) == 0x80); i++) {}
        if (i != sequence) sequence = 1;
        used += sequence;
        columns++;
    }
    if (bytes) *bytes = used;
    return columns;
}

static void box_repeat(const char *character, size_t count)
{
    while (count--) fputs(character, stdout);
}

static void box_open(const char *title)
{
    size_t title_width = utf8_prefix(title, BOX_INNER_WIDTH - 2, NULL);
    size_t remaining = BOX_INNER_WIDTH - title_width - 2;
    fputs("╭", stdout);
    box_repeat("─", remaining / 2);
    printf(" %s ", title);
    box_repeat("─", remaining - remaining / 2);
    puts("╮");
}

static void box_line(const char *text)
{
    size_t bytes = 0;
    size_t width = utf8_prefix(text, BOX_INNER_WIDTH - 2, &bytes);
    fputs("│  ", stdout);
    fwrite(text, 1, bytes, stdout);
    box_repeat(" ", BOX_INNER_WIDTH - 2 - width);
    puts("│");
}

static void box_close(void)
{
    fputs("╰", stdout);
    box_repeat("─", BOX_INNER_WIDTH);
    puts("╯");
}

static void print_success(const char *message)
{
    if (!unicode_ui_enabled()) { puts(message); return; }
    box_open("SUCESSO");
    box_line(message);
    box_close();
}

static void print_error_message(const char *message)
{
    if (!unicode_ui_enabled()) { fprintf(stderr, "%s\n", message); return; }
    box_open("ERRO");
    box_line(message);
    box_close();
}

static void print_record_begin(const char *title)
{
    if (unicode_ui_enabled()) box_open(title);
    else printf("%s\n", title);
}

static void print_record_field(const char *label, const char *value)
{
    if (unicode_ui_enabled()) {
        char line[256];
        snprintf(line, sizeof line, "%s %s", label, value);
        box_line(line);
    } else printf("  %s: %s\n", label, value);
}

static void print_record_end(void)
{
    if (unicode_ui_enabled()) box_close();
}

static void print_list(const char *json)
{
    const char *item = strstr(json, "{\"id\":");
    int users = strstr(json, "\"phone\":") != NULL;
    int classes = !users && strstr(json, "\"grade\":") != NULL;
    int tasks = !users && !classes && strstr(json, "\"subject\":") != NULL;
    if (!unicode_ui_enabled()) {
        if (users) puts("USUÁRIOS CADASTRADOS\nID   NOME                     TELEFONE           TIPO         SITUAÇÃO");
        else if (classes) puts("TURMAS CADASTRADAS\nID   NOME                     SÉRIE        TURNO");
        else if (tasks) puts("TAREFAS\nID   MATÉRIA            TÍTULO                         ENTREGA");
        if (!item) { puts("Nenhum registro encontrado."); return; }
        while (item) {
            char id[16] = "-", name[128] = "-", phone[40] = "-", type[20] = "-", status[20] = "-";
            char grade[40] = "-", shift[20] = "-", subject[80] = "-", title[140] = "-", due[20] = "-";
            (void)json_value(item, "id", id, sizeof id);
            if (users) {
                (void)json_value(item, "name", name, sizeof name); (void)json_value(item, "phone", phone, sizeof phone);
                (void)json_value(item, "type", type, sizeof type); (void)json_value(item, "status", status, sizeof status);
                printf("%-4s %-24.24s %-18.18s %-12.12s %-10.10s\n", id, name, phone, user_type_pt(type), status_pt(status));
            } else if (classes) {
                (void)json_value(item, "name", name, sizeof name); (void)json_value(item, "grade", grade, sizeof grade);
                (void)json_value(item, "shift", shift, sizeof shift);
                printf("%-4s %-24.24s %-12.12s %-12.12s\n", id, name, grade, shift_pt(shift));
            } else if (tasks) {
                (void)json_value(item, "subject", subject, sizeof subject); (void)json_value(item, "title", title, sizeof title);
                (void)json_value(item, "due_date", due, sizeof due);
                printf("%-4s %-18.18s %-30.30s %-12.12s\n", id, subject, title, due);
            }
            item = strstr(item + 1, "{\"id\":");
        }
        return;
    }
    if (users) {
        box_open("USUÁRIOS CADASTRADOS");
        box_line("ID   NOME                     TELEFONE           TIPO         SITUAÇÃO");
    } else if (classes) {
        box_open("TURMAS CADASTRADAS");
        box_line("ID   NOME                     SÉRIE        TURNO");
    } else if (tasks) {
        box_open("TAREFAS");
        box_line("ID   MATÉRIA            TÍTULO                         ENTREGA");
    } else {
        box_open("RESULTADOS");
    }
    box_line("─────────────────────────────────────────────────────────────────────────");
    if (!item) {
        box_line("Nenhum registro encontrado.");
        box_close();
        return;
    }
    while (item) {
        char id[16] = "-", name[128] = "-", phone[40] = "-", type[20] = "-", status[20] = "-";
        char grade[40] = "-", shift[20] = "-", subject[80] = "-", title[140] = "-", due[20] = "-";
        (void)json_value(item, "id", id, sizeof id);
        if (users) {
            (void)json_value(item, "name", name, sizeof name); (void)json_value(item, "phone", phone, sizeof phone);
            (void)json_value(item, "type", type, sizeof type); (void)json_value(item, "status", status, sizeof status);
            char row[256];
            snprintf(row, sizeof row, "%-4s %-24.24s %-18.18s %-12.12s %-10.10s", id, name, phone,
                     user_type_pt(type), status_pt(status));
            box_line(row);
        } else if (classes) {
            (void)json_value(item, "name", name, sizeof name); (void)json_value(item, "grade", grade, sizeof grade);
            (void)json_value(item, "shift", shift, sizeof shift);
            char row[256];
            snprintf(row, sizeof row, "%-4s %-24.24s %-12.12s %-12.12s", id, name, grade, shift_pt(shift));
            box_line(row);
        } else if (tasks) {
            (void)json_value(item, "subject", subject, sizeof subject); (void)json_value(item, "title", title, sizeof title);
            (void)json_value(item, "due_date", due, sizeof due);
            char row[256];
            snprintf(row, sizeof row, "%-4s %-18.18s %-30.30s %-12.12s", id, subject, title, due);
            box_line(row);
        }
        item = strstr(item + 1, "{\"id\":");
    }
    box_close();
}

static void print_response(const char *response)
{
    char value[128];
    if (strstr(response, "\"items\":[")) { print_list(response); return; }
    if (strstr(response, "\"error\":")) {
        char code[16] = "-";
        (void)json_value(response, "error", code, sizeof code);
        if (json_value(response, "message", value, sizeof value)) {
            char message[160];
            snprintf(message, sizeof message, "Erro %s: %s", code, value);
            print_error_message(message);
        } else
            print_error_message("Ocorreu um erro. Tente novamente.");
        return;
    }
    if (strstr(response, "\"status\":\"sent\"")) { print_success("Mensagem enviada com sucesso."); return; }
    if (strstr(response, "\"status\":\"assigned\"")) { print_success("Aluno vinculado à turma com sucesso."); return; }
    if (strstr(response, "\"message\":\"user updated\"")) { print_success("Usuário atualizado com sucesso."); return; }
    if (strstr(response, "\"message\":\"user deleted\"")) { print_success("Usuário excluído com sucesso."); return; }
    if (strstr(response, "\"message\":\"class updated\"")) { print_success("Turma atualizada com sucesso."); return; }
    if (strstr(response, "\"message\":\"class deactivated\"")) { print_success("Turma inativada com sucesso; o histórico foi preservado."); return; }
    if (strstr(response, "\"message\":\"task updated\"")) { print_success("Tarefa atualizada com sucesso."); return; }
    if (strstr(response, "\"message\":\"task deleted\"")) { print_success("Tarefa excluída com sucesso."); return; }
    if (strstr(response, "\"ok\":true")) { print_success("Operação realizada com sucesso."); return; }
    if (strstr(response, "\"id\":") && strstr(response, "\"grade\":")) {
        char id[16] = "-", name[128] = "-", grade[40] = "-", shift[20] = "-";
        (void)json_value(response, "id", id, sizeof id);
        (void)json_value(response, "name", name, sizeof name);
        (void)json_value(response, "grade", grade, sizeof grade);
        (void)json_value(response, "shift", shift, sizeof shift);
        print_record_begin("TURMA CRIADA");
        print_record_field("ID:", id); print_record_field("Nome:", name);
        print_record_field("Série:", grade); print_record_field("Turno:", shift_pt(shift));
        print_record_end();
        return;
    }
    if (strstr(response, "\"id\":") && strstr(response, "\"phone\":")) {
        char id[16] = "-", name[128] = "-", phone[40] = "-", type[20] = "-";
        (void)json_value(response, "id", id, sizeof id);
        (void)json_value(response, "name", name, sizeof name);
        (void)json_value(response, "phone", phone, sizeof phone);
        (void)json_value(response, "type", type, sizeof type);
        print_record_begin("USUÁRIO CRIADO");
        print_record_field("ID:", id); print_record_field("Nome:", name);
        print_record_field("Telefone:", phone); print_record_field("Tipo:", user_type_pt(type));
        print_record_end();
        return;
    }
    if (strstr(response, "\"id\":") && strstr(response, "\"subject\":")) {
        char id[16] = "-", subject[80] = "-", title[140] = "-", due[20] = "-";
        (void)json_value(response, "id", id, sizeof id);
        (void)json_value(response, "subject", subject, sizeof subject);
        (void)json_value(response, "title", title, sizeof title);
        (void)json_value(response, "due_date", due, sizeof due);
        print_record_begin("TAREFA CRIADA");
        print_record_field("ID:", id); print_record_field("Matéria:", subject);
        print_record_field("Título:", title); print_record_field("Entrega:", due);
        print_record_end();
        return;
    }
    printf("%s\n", response);
}

static int path_is_public(const char *path)
{
    return strcmp(path, "/api/bootstrap") == 0 || strcmp(path, "/api/auth") == 0 ||
           strcmp(path, "/api/health") == 0;
}

static int call(const char *method, const char *path, const char *body)
{
    char response[65536];
    char token[80];
    if (!path_is_public(path) && !token_load(token, sizeof token)) {
        puts("Você precisa fazer login antes. Use sae login -p TELEFONE; a senha será solicitada sem eco.");
        return ERROR_UNAUTHORIZED;
    }
    if (!request(method, path, body, response, sizeof response)) {
        fprintf(stderr, "Nao foi possivel falar com o servidor. Inicie com: sae servidor\n"); return ERROR_NETWORK;
    }
    print_response(response);
    if (strstr(response, "\"error\":401")) return ERROR_UNAUTHORIZED;
    if (strstr(response, "\"error\":")) return ERROR_HTTP;
    return SUCCESS;
}

static int valid_positive_id(const char *value)
{
    if (!value || !*value) return 0;
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p)
        if (*p < '0' || *p > '9') return 0;
    errno = 0;
    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    return errno != ERANGE && end && *end == '\0' && parsed > 0;
}

static int escola_tutor_reply(const char *message_id, const char *class_id,
                              const char *question)
{
    if (!message_id || !valid_positive_id(class_id) || !question ||
        !validation_length_between(question, 3, 800)) {
        fputs("Use: sae escola tutor -i ID_DA_INBOX -c ID_TURMA -m PERGUNTA\n", stderr);
        return ERROR_INVALID_INPUT;
    }
    AppConfig cfg;
    if (config_load(&cfg) != SUCCESS || !ai_remote_enabled()) {
        fputs("Configure AI_API_KEY, AI_MODEL e AI_API_URL no .env para usar a tutoria.\n", stderr);
        return ERROR_CONFIG;
    }

    char path[128], tasks_json[65536];
    int n = snprintf(path, sizeof path, "/api/tasks?class_id=%s", class_id);
    if (n < 0 || (size_t)n >= sizeof path) return ERROR_TOO_LONG;
    char token[80];
    if (!token_load(token, sizeof token)) {
        fputs("Faça login no SAE para consultar tarefas dessa turma.\n", stderr);
        return ERROR_UNAUTHORIZED;
    }
    if (!request("GET", path, "", tasks_json, sizeof tasks_json)) {
        fputs("Não foi possível consultar as tarefas no SAE. Inicie o servidor com: sae servidor\n", stderr);
        return ERROR_NETWORK;
    }
    if (strstr(tasks_json, "\"error\":")) {
        print_response(tasks_json);
        return strstr(tasks_json, "\"error\":401") ? ERROR_UNAUTHORIZED : ERROR_HTTP;
    }

    char answer[4001];
    int rc = ai_answer_student_question(question, tasks_json, answer, sizeof answer);
    if (rc != SUCCESS || !answer[0]) {
        print_error_message("A IA não conseguiu preparar uma resposta para essa pergunta.");
        return rc == SUCCESS ? ERROR_AI : rc;
    }
    return escola_api_reply(message_id, answer);
}

int cli_run(int argc, char **argv)
{
    if (argc < 2) { help(); return SUCCESS; }
    if (strcmp(argv[1], "servidor") == 0) return -1;
    if (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h") || !strcmp(argv[1], "ajuda")) { help(); return SUCCESS; }
    if (!strcmp(argv[1], "escola") && argc >= 3) {
        if (!strcmp(argv[2], "whoami") && argc == 3) return escola_api_whoami();
        if (!strcmp(argv[2], "ref") && argc == 3) return escola_api_ref();
        if (!strcmp(argv[2], "inbox")) {
            const char *limit_arg = opt(argc, argv, "-l");
            int limit = 20;
            if (limit_arg) {
                char *end = NULL;
                long parsed = strtol(limit_arg, &end, 10);
                if (!end || *end || parsed < 1 || parsed > 50) return ERROR_INVALID_INPUT;
                limit = (int)parsed;
            }
            return escola_api_inbox(limit);
        }
        if (!strcmp(argv[2], "reply"))
            return escola_api_reply(opt(argc, argv, "-i"), opt(argc, argv, "-m"));
        if (!strcmp(argv[2], "tutor"))
            return escola_tutor_reply(opt(argc, argv, "-i"), opt(argc, argv, "-c"),
                                      opt(argc, argv, "-m"));
        fprintf(stderr, "Use: sae escola whoami|inbox [-l LIMITE]|reply -i ID -m TEXTO|tutor -i ID -c TURMA -m PERGUNTA|ref\n");
        return ERROR_INVALID_INPUT;
    }
    const char *n = opt(argc, argv, "-n"), *p = opt(argc, argv, "-p"), *s = opt(argc, argv, "-s");
    const char *t = opt(argc, argv, "-t"), *c = opt(argc, argv, "-c"), *u = opt(argc, argv, "-u");
    const char *m = opt(argc, argv, "-m"), *v = opt(argc, argv, "-v"), *d = opt(argc, argv, "-d"), *g = opt(argc, argv, "-g"), *i = opt(argc, argv, "-i");
    char a[128] = {0}, b[128] = {0}, secret[128] = {0}, q[8192] = {0};
    if ((strcmp(argv[1], "-ca") == 0 || strcmp(argv[1], "--create-admin") == 0) && n && p) {
        if (s) {
            fprintf(stderr, "Por segurança, não passe senha como argumento. Omita -s para digitá-la sem eco.\n");
            return ERROR_INVALID_INPUT;
        }
        const char *setup_key = getenv("SAE_BOOTSTRAP_KEY");
        char key_json[256], password_json[128];
        if (!setup_key || !*setup_key) {
            fprintf(stderr, "Defina SAE_BOOTSTRAP_KEY no ambiente para criar o primeiro administrador.\n");
            return ERROR_CONFIG;
        }
        if (!prompt_password("Senha do administrador: ", secret, sizeof secret))
            return ERROR_INVALID_INPUT;
        if (!json_text(n,a,sizeof a)||!json_text(p,b,sizeof b)||
            !json_text(secret,password_json,sizeof password_json)||
            !json_text(setup_key,key_json,sizeof key_json)) {
            clear_secret(secret, sizeof secret);
            return ERROR_INVALID_INPUT;
        }
        snprintf(q,sizeof q,"{\"name\":\"%s\",\"phone\":\"%s\",\"password\":\"%s\",\"bootstrap_key\":\"%s\"}",a,b,password_json,key_json);
        clear_secret(secret, sizeof secret);
        clear_secret(password_json, sizeof password_json);
        int rc = call("POST","/api/bootstrap",q);
        clear_secret(q, sizeof q);
        clear_secret(key_json, sizeof key_json);
        return rc;
    }
    if (!strcmp(argv[1], "login") && p) {
        if (s) {
            fprintf(stderr, "Por segurança, não passe senha como argumento. Omita -s para digitá-la sem eco.\n");
            return ERROR_INVALID_INPUT;
        }
        if (!prompt_password("Senha: ", secret, sizeof secret)) return ERROR_INVALID_INPUT;
        if (!json_text(p,a,sizeof a)||!json_text(secret,b,sizeof b)) {
            clear_secret(secret, sizeof secret);
            return ERROR_INVALID_INPUT;
        }
        clear_secret(secret, sizeof secret);
        snprintf(q,sizeof q,"{\"phone\":\"%s\",\"password\":\"%s\"}",a,b);
        char r[4096] = {0};
        if (!request("POST","/api/auth",q,r,sizeof r)) {
            clear_secret(b, sizeof b); clear_secret(q, sizeof q);
            return ERROR_NETWORK;
        }
        clear_secret(b, sizeof b); clear_secret(q, sizeof q);
        char *x=strstr(r,"\"token\":\"");
        if (!x) { print_response(r); clear_secret(r, sizeof r); return ERROR_UNAUTHORIZED; }
        x += 9;
        char *end = strchr(x, '"');
        if (!end) { clear_secret(r, sizeof r); return ERROR_HTTP; }
        *end = '\0';
        int saved = token_save_secure(x);
        clear_secret(r, sizeof r);
        if (!saved) return ERROR_INTERNAL;
        print_success("Login realizado. Sessão salva neste computador.");
        return SUCCESS;
    }
    if (!strcmp(argv[1], "logout")) {
        char response[4096];
        if (!token_load(a, sizeof a)) {
            print_success("Você não está logado neste computador.");
            return SUCCESS;
        }
        if (!request("POST", "/api/auth/logout", "", response, sizeof response)) {
            fprintf(stderr, "Nao foi possivel falar com o servidor. A sessao local foi removida.\n");
            (void)remove(SESSION_FILE);
            return ERROR_NETWORK;
        }
        (void)remove(SESSION_FILE);
        if (strstr(response, "\"ok\":true")) {
            print_success("Logout realizado com sucesso.");
            return SUCCESS;
        }
        print_success("Sessão local removida. O servidor não reconheceu o token.");
        return SUCCESS;
    }
    if (!strcmp(argv[1], "usuario") && argc > 2 && !strcmp(argv[2], "listar")) return call("GET","/api/users","");
    if (!strcmp(argv[1], "usuario") && argc > 2 && !strcmp(argv[2], "criar") && n && p && t) {
        char type_json[16], password_json[128];
        if (s) {
            fprintf(stderr, "Por segurança, não passe senha como argumento. A senha será solicitada sem eco.\n");
            return ERROR_INVALID_INPUT;
        }
        if (!prompt_password("Senha do usuário: ", secret, sizeof secret)) return ERROR_INVALID_INPUT;
        if (!json_text(n, a, sizeof a) || !json_text(p, b, sizeof b) ||
            !json_text(t, type_json, sizeof type_json) ||
            !json_text(secret, password_json, sizeof password_json)) {
            clear_secret(secret, sizeof secret);
            return ERROR_INVALID_INPUT;
        }
        clear_secret(secret, sizeof secret);
        snprintf(q, sizeof q, "{\"name\":\"%s\",\"phone\":\"%s\",\"type\":\"%s\",\"password\":\"%s\"}", a, b, type_json, password_json);
        clear_secret(password_json, sizeof password_json);
        int rc = call("POST", "/api/users", q);
        clear_secret(q, sizeof q);
        return rc;
    }
    if (!strcmp(argv[1], "usuario") && argc > 2 && !strcmp(argv[2], "editar") && u && n && p) {
        if (s) {
            fprintf(stderr, "Por segurança, não passe senha como argumento; use a tela de coordenação para redefini-la.\n");
            return ERROR_INVALID_INPUT;
        }
        char name_json[128], phone_json[32], password_json[128] = "";
        if (has_flag(argc, argv, "--senha") &&
            !prompt_password("Nova senha do usuário: ", secret, sizeof secret))
            return ERROR_INVALID_INPUT;
        if (!json_text(n, name_json, sizeof name_json) ||
            !json_text(p, phone_json, sizeof phone_json) ||
            (secret[0] && !json_text(secret, password_json, sizeof password_json))) {
            clear_secret(secret, sizeof secret);
            return ERROR_INVALID_INPUT;
        }
        snprintf(a, sizeof a, "/api/users/%s", u);
        snprintf(q, sizeof q, "{\"name\":\"%s\",\"phone\":\"%s\",\"password\":\"%s\"}", name_json, phone_json, password_json);
        clear_secret(secret, sizeof secret);
        clear_secret(password_json, sizeof password_json);
        int rc = call("PUT", a, q);
        clear_secret(q, sizeof q);
        return rc;
    }
    if (!strcmp(argv[1], "usuario") && argc > 2 && !strcmp(argv[2], "excluir") && u) {
        snprintf(a, sizeof a, "/api/users/%s", u);
        return call("DELETE", a, "");
    }
    if (!strcmp(argv[1], "aluno") && argc > 2 && !strcmp(argv[2], "vincular") && u && c) {
        snprintf(q, sizeof q, "{\"user_id\":%s,\"class_id\":%s}", u, c);
        return call("POST", "/api/students/class", q);
    }
    if (!strcmp(argv[1], "turma") && argc > 2 && !strcmp(argv[2], "listar")) return call("GET","/api/classes","");
    if (!strcmp(argv[1], "turma") && argc > 2 && !strcmp(argv[2], "criar") && n && g) {
        const char *turn = turn_to_api(t);
        snprintf(q,sizeof q,"{\"name\":\"%s\",\"grade\":\"%s\",\"shift\":\"%s\"}",n,g,turn); return call("POST","/api/classes",q);
    }
    if (!strcmp(argv[1], "turma") && argc > 2 && !strcmp(argv[2], "editar") && i && n && g && t) {
        snprintf(a, sizeof a, "/api/classes/%s", i);
        snprintf(q, sizeof q, "{\"name\":\"%s\",\"grade\":\"%s\",\"shift\":\"%s\"}", n, g, turn_to_api(t));
        return call("PUT", a, q);
    }
    if (!strcmp(argv[1], "turma") && argc > 2 && !strcmp(argv[2], "excluir") && i) {
        snprintf(a, sizeof a, "/api/classes/%s", i);
        return call("DELETE", a, "");
    }
    if (!strcmp(argv[1], "turma") && argc > 2 && !strcmp(argv[2], "vincular") && c && u) { snprintf(q,sizeof q,"{\"teacher_user_id\":%s}",u); snprintf(a,sizeof a,"/api/classes/%s/teachers",c); return call("POST",a,q); }
    if (!strcmp(argv[1], "tarefa") && argc > 2 && !strcmp(argv[2], "listar") && c) { snprintf(a,sizeof a,"/api/tasks?class_id=%s",c); return call("GET",a,""); }
    if (!strcmp(argv[1], "tarefa") && argc > 2 && !strcmp(argv[2], "criar") && c && m && n && v) {
        if (u) snprintf(q, sizeof q, "{\"class_id\":%s,\"teacher_id\":%s,\"subject\":\"%s\",\"title\":\"%s\",\"description\":\"%s\",\"due_date\":\"%s\"}", c, u, m, n, d ? d : "", v);
        else snprintf(q, sizeof q, "{\"class_id\":%s,\"subject\":\"%s\",\"title\":\"%s\",\"description\":\"%s\",\"due_date\":\"%s\"}", c, m, n, d ? d : "", v);
        return call("POST", "/api/tasks", q);
    }
    if (!strcmp(argv[1], "tarefa") && argc > 2 && !strcmp(argv[2], "editar") && i && m && n && v) {
        snprintf(a, sizeof a, "/api/tasks/%s", i);
        snprintf(q, sizeof q, "{\"subject\":\"%s\",\"title\":\"%s\",\"description\":\"%s\",\"due_date\":\"%s\"}", m, n, d ? d : "", v);
        return call("PUT", a, q);
    }
    if (!strcmp(argv[1], "tarefa") && argc > 2 && !strcmp(argv[2], "excluir") && i) {
        snprintf(a, sizeof a, "/api/tasks/%s", i);
        return call("DELETE", a, "");
    }
    if (!strcmp(argv[1], "whatsapp") && argc >= 3) {
        if (!strcmp(argv[2], "enviar") && i && m)
            return escola_api_reply(i, m);
        if (!strcmp(argv[2], "turma")) {
            fputs("A API de mensagens atual só permite responder a um ID da inbox; "
                  "ela não oferece envio por telefone nem envio para turma.\n", stderr);
            return ERROR_NOT_IMPLEMENTED;
        }
        fputs("Use: sae whatsapp enviar -i ID_DA_INBOX -m MENSAGEM\n"
              "O ID deve ser m_... e vir de `sae escola inbox`.\n", stderr);
        return ERROR_INVALID_INPUT;
    }
    fprintf(stderr,"Comando incompleto ou desconhecido. Use: sae --help\n"); return ERROR_INVALID_INPUT;
}
