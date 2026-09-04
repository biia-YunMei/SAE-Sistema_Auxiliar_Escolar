/* whatsapp_adapter.h
 * Adapter concreto para WhatsApp Business Cloud (Meta).
 *
 * HTTP via libcurl. Este header é OPCIONAL e só deve ser incluído
 * se o sistema de build tiver libcurl.
 */
#ifndef SCHOOL_WHATSAPP_ADAPTER_H
#define SCHOOL_WHATSAPP_ADAPTER_H

#include "error.h"

/* Envia mensagem via API oficial. Requer configuração em .env:
 *   WHATSAPP_API_URL       — ex: https://graph.facebook.com/vXX.X
 *   WHATSAPP_API_TOKEN     — token de acesso
 *   WHATSAPP_PHONE_NUMBER_ID
 */
int whatsapp_send_via_api(const char *phone, const char *text);

#endif /* SCHOOL_WHATSAPP_ADAPTER_H */
