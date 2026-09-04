/* whatsapp_adapter.c
 * Implementação concreta do envio via WhatsApp Business Cloud.
 *
 * Compilação condicional: se libcurl não estiver disponível, o arquivo
 * ainda compila mas `whatsapp_send_via_api` retorna ERROR_NOT_IMPLEMENTED.
 *
 *   cc -DWHATSAPP_USE_CURL ... -lcurl
 */
#include "whatsapp_adapter.h"
#include "logger.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(WHATSAPP_USE_CURL)
#include <curl/curl.h>

struct MemBuf { char *data; size_t size; };
static size_t cb_write(void *ptr, size_t s, size_t n, void *u)
{
    size_t total = s * n;
    struct MemBuf *m = (struct MemBuf *)u;
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
    if (!phone || !text) return ERROR_INVALID_INPUT;

#if !defined(WHATSAPP_USE_CURL)
    log_warn("whatsapp_send_via_api: libcurl indisponível, mensagem não enviada");
    (void)phone;
    return ERROR_NOT_IMPLEMENTED;
#else
    const AppConfig *cfg = config_current();
    if (!cfg) return ERROR_CONFIG;

    if (!cfg->whatsapp_api_url[0] || !cfg->whatsapp_api_token[0] ||
        !cfg->whatsapp_phone_id[0]) {
        log_warn("whatsapp: credenciais ausentes no .env");
        return ERROR_CONFIG;
    }

    char endpoint[768];
    int endpoint_len = snprintf(endpoint, sizeof endpoint, "%s/%s/messages",
                                cfg->whatsapp_api_url, cfg->whatsapp_phone_id);
    if (endpoint_len < 0 || (size_t)endpoint_len >= sizeof endpoint)
        return ERROR_TOO_LONG;

    /* Escapa o texto antes de inseri-lo no corpo JSON. */
    char escaped_text[24001];
    int escape_rc = json_escape(escaped_text, sizeof escaped_text, text);
    if (escape_rc != SUCCESS) return escape_rc;

    /* A Cloud API recebe o número em formato E.164 sem o sinal '+'. */
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
    log_info("whatsapp: enviado para %s (http %ld)", phone, http);
    return SUCCESS;
#endif
}
