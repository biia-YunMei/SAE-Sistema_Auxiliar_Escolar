/* whatsapp.h
 * Abstração do canal WhatsApp.
 *
 * O back-end NUNCA importa a API do WhatsApp diretamente.
 * Esta interface é implementada por um adapter concreto (Etapa 6),
 * permitindo trocar o provedor sem mexer em controllers/serviços.
 */
#ifndef SCHOOL_WHATSAPP_H
#define SCHOOL_WHATSAPP_H

#include "error.h"
#include <stddef.h>

int  whatsapp_init(void);                       /* carrega config, conecta */
void whatsapp_shutdown(void);

int  whatsapp_send_message(const char *phone, const char *text);

/* Recebe e processa uma mensagem. Retorna resposta sugerida em reply_out.
 * Implementação stub segura: valida telefone e ecoa. */
int  whatsapp_receive_message(const char *from_phone, const char *body,
                              char *reply_out, size_t reply_cap);

#endif /* SCHOOL_WHATSAPP_H */
