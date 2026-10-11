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
#include <stdint.h>

int  whatsapp_init(void);                       /* carrega config, conecta */
void whatsapp_shutdown(void);

int  whatsapp_send_message(const char *phone, const char *text);

/* Recebe e processa uma mensagem. Retorna resposta sugerida em reply_out.
 * Implementação stub segura: valida telefone e ecoa. */
int  whatsapp_receive_message(const char *from_phone, const char *body,
                              char *reply_out, size_t reply_cap);

/* Mensagens do inbox escolar chegam com identificador anônimo estável
 * (from_ref), nunca com telefone. A turma e o estado de pergunta ficam no SQLite. */
int  whatsapp_receive_anonymous_message(const char *from_ref, const char *body,
                                        char *reply_out, size_t reply_cap);
/* Snapshot curto de turmas ativas e próximas tarefas para o contexto da IA. */
void whatsapp_build_school_context(char *out, size_t cap);
void whatsapp_build_school_context_for_class(int64_t class_id, char *out, size_t cap);
int  whatsapp_store_anonymous_inbox_message(const char *message_id,
                                            const char *from_ref,
                                            const char *body,
                                            const char *kind);
int  whatsapp_mark_anonymous_inbox_message(const char *message_id,
                                           const char *status);

#endif /* SCHOOL_WHATSAPP_H */
