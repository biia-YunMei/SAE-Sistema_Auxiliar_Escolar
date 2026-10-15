/* whatsapp_adapter.c
 * Implementação concreta do envio via API externa de mensagens.
 *
 * Compilação condicional: se libcurl não estiver disponível, o arquivo
 * ainda compila mas `whatsapp_send_via_api` retorna ERROR_NOT_IMPLEMENTED.
 *
 *   cc -DWHATSAPP_USE_CURL ... -lcurl
 */
#include "whatsapp_adapter.h"
#include "logger.h"
#include "config.h"
#include "validation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#if defined(WHATSAPP_USE_CURL)
#include <curl/curl.h>

struct MemBuf { char *data; size_t size; };
#define WHATSAPP_RESPONSE_MAX (1024U * 1024U)

static int whatsapp_url_is_secure(const char *url)
{
    if (!url || strncmp(url, "https://", 8) != 0) return 0;
    for (const unsigned char *p = (const unsigned char *)url; *p; ++p)
        if (*p <= 0x20 || *p == 0x7f || *p == '\\' || *p == '#') return 0;
    const unsigned char *host = (const unsigned char *)url + 8;
    if (!*host || *host == '/' || *host == '?' || *host == '#') return 0;
    for (const unsigned char *p = host; *p && *p != '/' && *p != '?'; ++p) {
        if (*p == '@') return 0;
    }
    return 1;
}

static size_t cb_write(void *ptr, size_t s, size_t n, void *u)
{
    if (s && n > SIZE_MAX / s) return 0;
    size_t total = s * n;
    struct MemBuf *m = (struct MemBuf *)u;
    if (m->size > WHATSAPP_RESPONSE_MAX || total > WHATSAPP_RESPONSE_MAX - m->size)
        return 0;
    char *p = realloc(m->data, m->size + total + 1);
    if (!p) return 0;
    m->data = p;
    memcpy(m->data + m->size, ptr, total);
    m->size += total;
    m->data[m->size] = '\0';
    return total;
}

static int json_escape(char *out, size_t cap, const char *text)
{
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        const char *escape = NULL;
        char unicode[7];
        switch (*p) {
        case '"':  escape = "\\\""; break;
        case '\\': escape = "\\\\"; break;
        case '\b': escape = "\\b"; break;
        case '\f': escape = "\\f"; break;
        case '\n': escape = "\\n"; break;
        case '\r': escape = "\\r"; break;
        case '\t': escape = "\\t"; break;
        default:
            if (*p < 0x20) {
                (void)snprintf(unicode, sizeof unicode, "\\u%04x", *p);
                escape = unicode;
            }
            break;
        }
        if (escape) {
            size_t len = strlen(escape);
            if (used + len >= cap) return ERROR_TOO_LONG;
            memcpy(out + used, escape, len);
            used += len;
        } else {
            if (used + 1 >= cap) return ERROR_TOO_LONG;
            out[used++] = (char)*p;
        }
    }
    out[used] = '\0';
    return SUCCESS;
}
#endif

int whatsapp_send_via_api(const char *phone, const char *text)
{
    if (!phone || !text || !validation_is_valid_phone(phone)) return ERROR_INVALID_INPUT;

#if !defined(WHATSAPP_USE_CURL)
    log_warn("whatsapp_send_via_api: libcurl indisponível, mensagem não enviada");
    (void)phone;
    return ERROR_NOT_IMPLEMENTED;
#else
    const AppConfig *cfg = config_current();
    if (!cfg) return ERROR_CONFIG;

    if (!cfg->whatsapp_api_url[0] || !cfg->whatsapp_api_token[0] ||
        !cfg->whatsapp_phone_id[0] || !whatsapp_url_is_secure(cfg->whatsapp_api_url)) {
        log_warn("whatsapp: credenciais ausentes no .env");
        return ERROR_CONFIG;
    }
    for (const unsigned char *p = (const unsigned char *)cfg->whatsapp_phone_id; *p; ++p)
        if (!((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= 'a' && *p <= 'z') || *p == '-' || *p == '_'))
            return ERROR_CONFIG;
    for (const unsigned char *p = (const unsigned char *)cfg->whatsapp_api_token; *p; ++p)
        if (*p == '\r' || *p == '\n' || *p == 0x7f) return ERROR_CONFIG;

    char endpoint[768];
    int endpoint_len = snprintf(endpoint, sizeof endpoint, "%s/%s/messages",
                                cfg->whatsapp_api_url, cfg->whatsapp_phone_id);
    if (endpoint_len < 0 || (size_t)endpoint_len >= sizeof endpoint)
        return ERROR_TOO_LONG;

    /* Escapa o texto antes de inseri-lo no corpo JSON. */
    char escaped_text[24001];
    int escape_rc = json_escape(escaped_text, sizeof escaped_text, text);
    if (escape_rc != SUCCESS) return escape_rc;

    /* A compatible API recebe o número em formato E.164 sem o sinal '+'. */
    const char *recipient = phone[0] == '+' ? phone + 1 : phone;
    char body[24576];
    int n = snprintf(body, sizeof body,
        "{\"messaging_product\":\"whatsapp\",\"to\":\"%s\",\"type\":\"text\","
        "\"text\":{\"body\":\"%s\"}}", recipient, escaped_text);
    if (n < 0 || (size_t)n >= sizeof body) return ERROR_TOO_LONG;

    CURL *c = curl_easy_init();
    if (!c) return ERROR_NETWORK;

    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
    char auth_hdr[600];
    int auth_len = snprintf(auth_hdr, sizeof auth_hdr, "Authorization: Bearer %s",
                            cfg->whatsapp_api_token);
    if (auth_len < 0 || (size_t)auth_len >= sizeof auth_hdr) {
        curl_slist_free_all(hdrs);
        curl_easy_cleanup(c);
        return ERROR_TOO_LONG;
    }
    hdrs = curl_slist_append(hdrs, auth_hdr);

    struct MemBuf mem = {0};
    curl_easy_setopt(c, CURLOPT_URL, endpoint);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(c, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, cb_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &mem);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);

    CURLcode rc = curl_easy_perform(c);
    long http = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);
    free(mem.data);

    if (rc != CURLE_OK) {
        log_error("whatsapp: curl falhou: %s", curl_easy_strerror(rc));
        return ERROR_NETWORK;
    }
    if (http < 200 || http >= 300) {
        log_error("whatsapp: HTTP %ld", http);
        return ERROR_WHATSAPP;
    }
    log_info("whatsapp: mensagem enviada (http %ld)", http);
    return SUCCESS;
#endif
}
