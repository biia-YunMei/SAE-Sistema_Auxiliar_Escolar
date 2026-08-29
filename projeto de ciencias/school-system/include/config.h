/* config.h
 * Carrega configuração a partir de variáveis de ambiente (ou .env simples).
 * Nenhum segredo deve aparecer no código-fonte.
 */
#ifndef SCHOOL_CONFIG_H
#define SCHOOL_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    /* HTTP */
    char  server_host[64];
    int   server_port;

    /* Banco */
    char  database_path[256];

    /* Logs */
    char  log_dir[256];
    char  log_level[8];           /* DEBUG | INFO | WARN | ERROR */

    /* Mídia */
    char  media_dir[256];

    /* WhatsApp (opcional) */
    char  whatsapp_api_url[256];
    char  whatsapp_api_token[256];
    char  whatsapp_phone_id[64];

    /* IA (opcional) */
    char  ai_api_url[256];
    char  ai_api_key[256];
    char  ai_model[64];

    /* Transcrição (opcional) */
    char  transcription_api_url[256];
    char  transcription_api_key[256];

    /* Limites de segurança */
    int   max_message_size;
    int   max_request_size;
    int   session_timeout_min;
} AppConfig;

/* Carrega do ambiente (e, se existir, do arquivo .env).
 * Retorna SUCCESS ou ERROR_CONFIG. */
int  config_load(AppConfig *out);

/* Helpers para uso em logs (não expõem segredos). */
const char *config_get_database_path(const AppConfig *cfg);
const char *config_get_log_dir(const AppConfig *cfg);
const char *config_get_log_level(const AppConfig *cfg);

#endif /* SCHOOL_CONFIG_H */
