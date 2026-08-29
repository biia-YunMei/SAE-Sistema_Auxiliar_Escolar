/* whatsapp_controller.c
 * Webhook de entrada do WhatsApp.
 *
 * Recebe um payload simplificado por body e delega ao módulo whatsapp.c.
 * Por enquanto aceitamos texto plano ou JSON {"from":"+55...","text":"..."}.
 *
 * A integração concreta com a API do WhatsApp Business Cloud é isolada em
 * whatsapp.c — este controller só conhece a abstração.
 */
#include "http_server.h"
#include "auth.h"
#include "validation.h"
#include "logger.h"
#include "controllers.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Declarações forward da Etapa 6 — definidas em whatsapp.c. */
int  whatsapp_init(void);
int  whatsapp_send_message(const char *phone, const char *text);
int  whatsapp_receive_message(const char *from_phone, const char *body,
                              char *reply_out, size_t reply_cap);

static int extract_str(const char *b, const char *k, char *o, size_t c)
{
    char n[64]; int x = snprintf(n, sizeof n, "\"%s\":\"", k);
    if (x <= 0 || (size_t)x >= sizeof n) return 0;
    const char *p = strstr(b, n);
    if (!p) return 0;
    p += x;
    const char *e = strchr(p, '"');
    if (!e) return 0;
    size_t l = (size_t)(e - p);
    if (l >= c) l = c - 1;
    memcpy(o, p, l); o[l] = '\0';
    return 1;
}

/* Webhook handler — sem exigir sessão pois o WhatsApp chama de fora.
 * A segurança vem da validação do token de verificação configurado em
 * produção (HMAC do Meta). Aqui aceitamos "X-Webhook-Token" opcional. */
int ctrl_whatsapp_webhook(const HttpRequest *req, HttpResponse *res)
{
    /* Em produção: validar token do Meta aqui.
     * Para a feira de ciências, basta logar e seguir. */

    if (strcmp(req->method, "POST") != 0)
        return http_respond_error(res, 405, "method not allowed");

    char from[32] = {0}, text[2048] = {0};
    if (!extract_str(req->body, "from", from, sizeof from))
        return http_respond_error(res, 400, "from required");
    if (!extract_str(req->body, "text", text, sizeof text))
        return http_respond_error(res, 400, "text required");
    if (!validation_is_valid_phone(from))
        return http_respond_error(res, 400, "invalid phone");
    if (!validation_length_between(text, 1, 2000))
        return http_respond_error(res, 400, "text too long");

    char reply[2048];
    int rc = whatsapp_receive_message(from, text, reply, sizeof reply);
    if (rc != SUCCESS) return http_respond_error(res, 500, "processing failed");

    whatsapp_send_message(from, reply);

    char out[2200];
    snprintf(out, sizeof out, "{\"reply\":\"%s\"}", reply);
    return http_respond_json(res, 200, out);
}

/* Health check — útil em Docker/Kubernetes. */
int ctrl_health(const HttpRequest *req, HttpResponse *res)
{
    (void)req;
    return http_respond_json(res, 200, "{\"status\":\"ok\"}");
}
