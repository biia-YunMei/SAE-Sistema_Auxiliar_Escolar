/* conversations.h
 * Máquina de estados que controla uma conversa por telefone.
 *
 *   IDLE → RECEIVING → PROCESSING → WAITING_AI → WAITING_TRANSCRIPTION
 *        → AWAITING_CONFIRMATION → CONFIRMED → SAVED
 *
 * Ramos:
 *   CANCELLED  — professor recusou
 *   ERROR      — algo falhou
 */
#ifndef SCHOOL_CONVERSATIONS_H
#define SCHOOL_CONVERSATIONS_H

#include "error.h"
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CONV_IDLE                  = 0,
    CONV_RECEIVING             = 1,
    CONV_PROCESSING            = 2,
    CONV_WAITING_AI            = 3,
    CONV_WAITING_TRANSCRIPTION = 4,
    CONV_AWAITING_CONFIRMATION = 5,
    CONV_CONFIRMED             = 6,
    CONV_SAVED                 = 7,
    CONV_CANCELLED             = 8,
    CONV_ERROR                 = 9
} ConversationState;

const char *conv_state_str(ConversationState s);

/* Cria ou recupera conversa de um telefone. */
int  conv_get_or_create(const char *phone, int64_t *out_id);

int  conv_set_state(int64_t id, ConversationState s);
int  conv_set_context(int64_t id, const char *json_context);   /* até 4 KB */
int  conv_get_state(int64_t id, ConversationState *out);

#endif /* SCHOOL_CONVERSATIONS_H */
