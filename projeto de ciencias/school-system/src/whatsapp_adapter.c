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
#endif

int whatsapp_send_via_api(const char *phone, const char *text)
{
    if (!phone || !text) return ERROR_INVALID_INPUT;

#if !defined(WHATSAPP_USE_CURL)
    log_warn("whatsapp_send_via_api: libcurl indisponível, mensagem não enviada");
    (void)phone;
    return ERROR_NOT_IMPLEMENTED;
#else
    /* Carrega config e envia via libcurl. */
    extern const AppConfig *app_config(void);
    const AppConfig *cfg = app_config();
    if (!cfg) return ERROR_CONFIG;

    char url[512] = {0}, token[512] = {0}, pid[64] = {0};
    strncpy(url,   cfg->whatsapp_api_url, sizeof url   - 1);
    strncpy(token, cfg->whatsapp_api_token, sizeof token - 1);
    strncpy(pid,   cfg->whatsapp_phone_id, sizeof pid - 1);
    if (!*url || !*token || !*pid) {
        log_warn("whatsapp: credenciais ausentes no .env");
        return ERROR_CONFIG;
    }

    char endpoint[768];
    snprintf(endpoint, sizeof endpoint, "%s/%s/messages", url, pid);

    /* Body JSON simples. Em produção, usar cJSON para escaping seguro. */
    char body[2048];
    int n = snprintf(body, sizeof body,
        "{\"messaging_product\":\"whatsapp\",\"to\":\"%s\",\"type\":\"text\","
        "\"text\":{\"body\":\"%s\"}}", phone, text);
    if (n < 0 || (size_t)n >= sizeof body) return ERROR_TOO_LONG;

    CURL *c = curl_easy_init();
    if (!c) return ERROR_NETWORK;

    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
    char auth_hdr[600];
    snprintf(auth_hdr, sizeof auth_hdr, "Authorization: Bearer %s", token);
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
