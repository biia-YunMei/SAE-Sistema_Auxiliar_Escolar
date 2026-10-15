/* config.c
 * Implementação da leitura de configuração.
 *
 * Estratégia:
 *   1. Tenta carregar /etc/school-backend.env e ./.env (apenas se existirem).
 *   2. Lê variáveis de ambiente, com defaults seguros.
 *   3. NÃO imprime segredos em logs.
 */
#include "config.h"
#include "error.h"

/* Habilita setenv() no glibc recente. Deve vir ANTES dos includes. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <process.h>
#define getpid _getpid
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#ifndef SAE_DEFAULT_WEB_ROOT
#define SAE_DEFAULT_WEB_ROOT "../../ScienceFair2026"
#endif

static AppConfig g_current_config;
static int g_config_loaded = 0;
static pthread_mutex_t g_bootstrap_env_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_bootstrap_cleanup_done = 0;

/* ---------- helpers internos -------------------------------------------- */

static void copy_str(char *dst, size_t cap, const char *src, const char *defv)
{
    const char *s = (src && *src) ? src : defv;
    if (!s) s = "";
    size_t n = strlen(s);
    if (n >= cap) n = cap - 1;
    memcpy(dst, s, n);
    dst[n] = '\0';
}

static int  parse_int_env(const char *name, int defv)
{
    const char *v = getenv(name);
    if (!v || !*v) return defv;
    errno = 0;
    char *end = NULL;
    long val = strtol(v, &end, 10);
    while (end && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) ++end;
    if (errno == ERANGE || end == v || (end && *end) || val < INT_MIN || val > INT_MAX)
        return defv;
    return (int)val;
}

/* Carrega um arquivo .env simples (apenas KEY=VALUE por linha).
 * Apenas preenche variáveis que ainda não estão definidas. */
static void load_env_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[1024];
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\n' || *p == '#') continue;

        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';

        char *key = p;
        char *val = eq + 1;
        size_t klen = strlen(key);
        while (klen > 0 && (key[klen-1] == ' ' || key[klen-1] == '\t')) {
            key[--klen] = '\0';
        }
        while (*val == ' ' || *val == '\t') ++val;
        size_t vlen = strlen(val);
        while (vlen > 0 && (val[vlen-1] == '\n' || val[vlen-1] == '\r' ||
                             val[vlen-1] == ' ' || val[vlen-1] == '\t'))
            val[--vlen] = '\0';
        if (vlen >= 2 && (val[0] == '"' || val[0] == '\'') && val[vlen-1] == val[0]) {
            val[vlen-1] = '\0';
            ++val;
        }

        if (getenv(key) == NULL && klen > 0) {
            /* Ambas as APIs copiam as strings; _putenv_s é o equivalente
             * no runtime Windows/MSYS2. */
#if defined(_WIN32)
            (void)_putenv_s(key, val);
#else
            (void)setenv(key, val, 0);
#endif
        }
    }
    fclose(f);
}

static int is_bootstrap_env_line(const char *line)
{
    while (*line == ' ' || *line == '\t') ++line;
    if (*line == '#' || *line == '\0' || *line == '\r' || *line == '\n') return 0;
    const char *equals = strchr(line, '=');
    if (!equals) return 0;
    const char *end = equals;
    while (end > line && (end[-1] == ' ' || end[-1] == '\t')) --end;
    static const char key[] = "SAE_BOOTSTRAP_KEY";
    return (size_t)(end - line) == sizeof key - 1 &&
           memcmp(line, key, sizeof key - 1) == 0;
}

int config_clear_bootstrap_key(void)
{
    /* Testes e ferramentas isoladas não devem alterar o .env do diretório. */
    if (!g_config_loaded) return SUCCESS;
    (void)pthread_mutex_lock(&g_bootstrap_env_mutex);
    if (g_bootstrap_cleanup_done) {
        (void)pthread_mutex_unlock(&g_bootstrap_env_mutex);
        return SUCCESS;
    }
#if defined(_WIN32)
    (void)_putenv_s("SAE_BOOTSTRAP_KEY", "");
#else
    (void)unsetenv("SAE_BOOTSTRAP_KEY");
#endif
    FILE *source = fopen(".env", "rb");
    if (!source) {
        int missing = errno == ENOENT;
        if (missing) g_bootstrap_cleanup_done = 1;
        (void)pthread_mutex_unlock(&g_bootstrap_env_mutex);
        return missing ? SUCCESS : ERROR_INTERNAL;
    }

    char temporary[96];
    int path_len = snprintf(temporary, sizeof temporary,
                            ".env.bootstrap.tmp.%ld", (long)getpid());
    if (path_len < 0 || (size_t)path_len >= sizeof temporary) {
        fclose(source);
        (void)pthread_mutex_unlock(&g_bootstrap_env_mutex);
        return ERROR_INTERNAL;
    }
#if defined(_WIN32)
    FILE *target = fopen(temporary, "wbx");
#else
    int target_fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
    FILE *target = target_fd >= 0 ? fdopen(target_fd, "wb") : NULL;
    if (!target && target_fd >= 0) (void)close(target_fd);
#endif
    if (!target) {
        fclose(source);
        (void)pthread_mutex_unlock(&g_bootstrap_env_mutex);
        return ERROR_INTERNAL;
    }

    struct stat file_stat;
    int have_mode = stat(".env", &file_stat) == 0;
    char line[4096];
    int at_line_start = 1, skip_line = 0, removed = 0;
    int failed = 0;
    while (fgets(line, sizeof line, source)) {
        if (at_line_start) {
            skip_line = is_bootstrap_env_line(line);
            if (skip_line) removed = 1;
        }
        if (!skip_line && fputs(line, target) == EOF) failed = 1;
        size_t length = strlen(line);
        at_line_start = length > 0 && line[length - 1] == '\n';
        if (at_line_start) skip_line = 0;
    }
    if (ferror(source) || fflush(target) != 0) failed = 1;
    if (fclose(source) != 0) failed = 1;
    if (fclose(target) != 0) failed = 1;

    if (!removed || failed) {
        (void)remove(temporary);
        if (!failed) g_bootstrap_cleanup_done = 1;
        (void)pthread_mutex_unlock(&g_bootstrap_env_mutex);
        return failed ? ERROR_INTERNAL : SUCCESS;
    }
#if !defined(_WIN32)
    if (have_mode && chmod(temporary, file_stat.st_mode & 0777) != 0) {
        (void)remove(temporary);
        (void)pthread_mutex_unlock(&g_bootstrap_env_mutex);
        return ERROR_INTERNAL;
    }
#else
    (void)have_mode;
#endif
    int replace_result = 0;
#if defined(_WIN32)
    if (remove(".env") != 0 && errno != ENOENT) replace_result = -1;
#endif
    if (replace_result == 0 && rename(temporary, ".env") != 0) replace_result = -1;
    if (replace_result != 0) (void)remove(temporary);
    if (replace_result == 0) g_bootstrap_cleanup_done = 1;
    (void)pthread_mutex_unlock(&g_bootstrap_env_mutex);
    return replace_result == 0 ? SUCCESS : ERROR_INTERNAL;
}

/* ---------- API pública -------------------------------------------------- */

int config_load(AppConfig *out)
{
    if (!out) return ERROR_INVALID_INPUT;
    memset(out, 0, sizeof *out);
    g_bootstrap_cleanup_done = 0;

    load_env_file(".env");
    load_env_file("/etc/school-backend.env");

    /* O backend HTTP não tem TLS. Mantém o socket apenas em loopback; o
     * navegador local e túneis reversos continuam funcionando sem expor a
     * porta diretamente na rede, mesmo que uma configuração antiga peça isso. */
    copy_str(out->server_host, sizeof out->server_host, NULL, "127.0.0.1");
    out->server_port = parse_int_env("SERVER_PORT", 8080);

    const char *database_path = getenv("DATABASE_PATH");
    char previous_home_database[sizeof out->database_path] = {0};
#if defined(_WIN32)
    const char *database_home = getenv("USERPROFILE");
    const char database_separator = '\\';
#else
    const char *database_home = getenv("HOME");
    const char database_separator = '/';
#endif
    if (database_home && *database_home) {
        int path_length = snprintf(previous_home_database, sizeof previous_home_database,
                                   "%s%cschool.db", database_home, database_separator);
        if (path_length >= 0 && (size_t)path_length < sizeof previous_home_database &&
            database_path && strcmp(database_path, previous_home_database) == 0)
            database_path = NULL;
    }
    copy_str(out->database_path, sizeof out->database_path,
             database_path, "data/school.db");
    copy_str(out->web_root, sizeof out->web_root, NULL, SAE_DEFAULT_WEB_ROOT);
    copy_str(out->log_dir,        sizeof out->log_dir,        getenv("LOG_DIR"),        "logs");
    copy_str(out->log_level,      sizeof out->log_level,      getenv("LOG_LEVEL"),      "INFO");
    copy_str(out->media_dir,      sizeof out->media_dir,      getenv("MEDIA_DIR"),      "data/media");

    copy_str(out->whatsapp_api_url,   sizeof out->whatsapp_api_url,   getenv("WHATSAPP_API_URL"),       "");
    copy_str(out->whatsapp_api_token, sizeof out->whatsapp_api_token, getenv("WHATSAPP_API_TOKEN"),     "");
    copy_str(out->whatsapp_phone_id,  sizeof out->whatsapp_phone_id,  getenv("WHATSAPP_PHONE_NUMBER_ID"), "");
    copy_str(out->whatsapp_verify_token, sizeof out->whatsapp_verify_token,
             getenv("WHATSAPP_VERIFY_TOKEN"), "");
    const char *webhook_secret = getenv("WHATSAPP_WEBHOOK_SECRET");
    if (!webhook_secret || !*webhook_secret)
        webhook_secret = getenv("WHATSAPP_APP_SECRET"); /* compatibilidade antiga */
    copy_str(out->whatsapp_webhook_secret, sizeof out->whatsapp_webhook_secret,
             webhook_secret, "");

    copy_str(out->ai_provider, sizeof out->ai_provider, getenv("AI_PROVIDER"), "gemini");
    copy_str(out->ai_api_url, sizeof out->ai_api_url, getenv("AI_API_URL"), "https://generativelanguage.googleapis.com/v1beta/models");
    copy_str(out->ai_api_key, sizeof out->ai_api_key, getenv("AI_API_KEY"), "");
    copy_str(out->ai_model,  sizeof out->ai_model,  getenv("AI_MODEL"),  "gemini-3.8-flash");
    copy_str(out->ai_instructions_file, sizeof out->ai_instructions_file,
             getenv("AI_INSTRUCTIONS_FILE"), "");

    copy_str(out->transcription_api_url, sizeof out->transcription_api_url, getenv("TRANSCRIPTION_API_URL"), "");
    copy_str(out->transcription_api_key, sizeof out->transcription_api_key, getenv("TRANSCRIPTION_API_KEY"), "");

    copy_str(out->escola_api_key, sizeof out->escola_api_key,
             getenv("ESCOLA_API_KEY"), "");
    copy_str(out->escola_cursor_path, sizeof out->escola_cursor_path,
             getenv("ESCOLA_CURSOR_PATH"), "data/escola_api.cursor");

    out->max_message_size   = parse_int_env("MAX_MESSAGE_SIZE",   4096);
    out->max_request_size   = parse_int_env("MAX_REQUEST_SIZE",   1048576);
    out->session_timeout_min= parse_int_env("SESSION_TIMEOUT_MIN", 60);

    /* Validação leve */
    if (out->server_port <= 0 || out->server_port > 65535 ||
        out->max_message_size < 1 || out->max_message_size > 4096 ||
        out->max_request_size < 1024 || out->max_request_size > 1048576 ||
        out->session_timeout_min < 1 || out->session_timeout_min > 1440) {
        return ERROR_CONFIG;
    }
    g_current_config = *out;
    g_config_loaded = 1;
    return SUCCESS;
}

const AppConfig *config_current(void)
{
    return g_config_loaded ? &g_current_config : NULL;
}

const char *config_get_database_path(const AppConfig *cfg) { return cfg ? cfg->database_path : ""; }
const char *config_get_log_dir(const AppConfig *cfg)        { return cfg ? cfg->log_dir        : ""; }
const char *config_get_log_level(const AppConfig *cfg)      { return cfg ? cfg->log_level      : ""; }
