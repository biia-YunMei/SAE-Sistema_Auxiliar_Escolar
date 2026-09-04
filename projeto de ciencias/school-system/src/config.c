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

static AppConfig g_current_config;
static int g_config_loaded = 0;

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
    char *end = NULL;
    long val = strtol(v, &end, 10);
    if (end == v) return defv;
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
        size_t vlen = strlen(val);
        while (vlen > 0 && (val[vlen-1] == '\n' || val[vlen-1] == '\r')) {
            val[--vlen] = '\0';
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

/* ---------- API pública -------------------------------------------------- */

int config_load(AppConfig *out)
{
    if (!out) return ERROR_INVALID_INPUT;
    memset(out, 0, sizeof *out);

    load_env_file(".env");
    load_env_file("/etc/school-backend.env");

    copy_str(out->server_host, sizeof out->server_host, getenv("SERVER_HOST"), "0.0.0.0");
    out->server_port = parse_int_env("SERVER_PORT", 8080);

    copy_str(out->database_path, sizeof out->database_path, getenv("DATABASE_PATH"), "data/school.db");
    copy_str(out->log_dir,        sizeof out->log_dir,        getenv("LOG_DIR"),        "logs");
    copy_str(out->log_level,      sizeof out->log_level,      getenv("LOG_LEVEL"),      "INFO");
    copy_str(out->media_dir,      sizeof out->media_dir,      getenv("MEDIA_DIR"),      "data/media");

    copy_str(out->whatsapp_api_url,   sizeof out->whatsapp_api_url,   getenv("WHATSAPP_API_URL"),       "");
    copy_str(out->whatsapp_api_token, sizeof out->whatsapp_api_token, getenv("WHATSAPP_API_TOKEN"),     "");
    copy_str(out->whatsapp_phone_id,  sizeof out->whatsapp_phone_id,  getenv("WHATSAPP_PHONE_NUMBER_ID"), "");
    copy_str(out->whatsapp_verify_token, sizeof out->whatsapp_verify_token,
             getenv("WHATSAPP_VERIFY_TOKEN"), "");
    copy_str(out->whatsapp_app_secret, sizeof out->whatsapp_app_secret,
             getenv("WHATSAPP_APP_SECRET"), "");

    copy_str(out->ai_api_url, sizeof out->ai_api_url, getenv("AI_API_URL"), "");
    copy_str(out->ai_api_key, sizeof out->ai_api_key, getenv("AI_API_KEY"), "");
    copy_str(out->ai_model,  sizeof out->ai_model,  getenv("AI_MODEL"),  "claude-sonnet-5");

    copy_str(out->transcription_api_url, sizeof out->transcription_api_url, getenv("TRANSCRIPTION_API_URL"), "");
    copy_str(out->transcription_api_key, sizeof out->transcription_api_key, getenv("TRANSCRIPTION_API_KEY"), "");

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
