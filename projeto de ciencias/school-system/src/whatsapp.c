/* whatsapp.c
 * Camada de abstração pública do canal WhatsApp.
 *
 * Na Etapa 5 funcionava como stub. Aqui, se o adapter concreto
 * (whatsapp_adapter.c compilado com -DWHATSAPP_USE_CURL) estiver
 * presente, usamos ele. Senão, caímos no stub seguro.
 */
#include "whatsapp.h"
#include "validation.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>

/* Declarado pelo adapter concreto (whatsapp_adapter.c). */
int whatsapp_send_via_api(const char *phone, const char *text);

int whatsapp_init(void)
{
    log_info("whatsapp: módulo inicializado");
    return SUCCESS;
}

void whatsapp_shutdown(void) { /* no-op */ }

int whatsapp_send_message(const char *phone, const char *text)
{
    if (!phone || !validation_is_valid_phone(phone)) return ERROR_INVALID_PHONE;
    if (!text) return ERROR_INVALID_INPUT;
    if (!validation_length_between(text, 1, 4000)) return ERROR_TOO_LONG;

#if defined(WHATSAPP_USE_CURL)
    int rc = whatsapp_send_via_api(phone, text);
    if (rc == SUCCESS) return SUCCESS;
    /* Fallback: nunca quebrar o sistema por causa do WhatsApp; só loga. */
    log_warn("whatsapp: falhou (%d), mensagem NÃO enviada", rc);
    (void)phone;
    return ERROR_WHATSAPP;
#else
    log_info("whatsapp: envio (stub) para %s len=%zu", phone, strlen(text));
    return SUCCESS;
#endif
}

int whatsapp_receive_message(const char *from_phone, const char *body,
                             char *reply_out, size_t reply_cap)
{
    if (!from_phone || !validation_is_valid_phone(from_phone))
        return ERROR_INVALID_PHONE;
    if (!body || !reply_out || reply_cap < 16)
        return ERROR_INVALID_INPUT;
    if (!validation_length_between(body, 1, 2000))
        return ERROR_TOO_LONG;

    log_info("whatsapp: recebido de %s len=%zu", from_phone, strlen(body));

    /* Stub: ecoa uma confirmação genérica.
     * As Etapas 7 e 8 substituem este comportamento pelo pipeline
     * auth → IA → confirm → DB. */
    int n = snprintf(reply_out, reply_cap, "Recebido. Processaremos em breve.");
    (void)n;
    return SUCCESS;
}
