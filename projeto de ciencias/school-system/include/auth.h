/* auth.h
 * Autenticação: atualmente baseada em número de telefone (canal WhatsApp).
 * Sessões são tokens opacos armazenados em memória com TTL.
 */
#ifndef SCHOOL_AUTH_H
#define SCHOOL_AUTH_H

#include "error.h"
#include "users.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int64_t  user_id;
    char     phone[32];
    UserType type;
    long     expires_at;   /* epoch seconds */
} AuthSession;

/* Autentica por telefone + (opcional) senha. */
int auth_login_phone(const char *phone, const char *password_or_null,
                     AuthSession *out);
int auth_login_phone_from_ip(const char *phone, const char *password_or_null,
                             const char *client_ip, AuthSession *out);

/* Cria token de sessão (opaco). */
int auth_session_create(AuthSession *s, char *token_out, size_t cap);

/* Resolve token → sessão (se válida e não expirada). */
int auth_session_resolve(const char *token, AuthSession *out);

/* Invalida sessão. */
int auth_session_revoke(const char *token);
/* Invalida todas as sessões ativas de uma conta após redefinição de senha. */
void auth_session_revoke_user(int64_t user_id);

/* Define a duracao das novas sessoes (1 a 1.440 minutos). */
void auth_set_session_timeout(int minutes);

void auth_shutdown(void);

#endif /* SCHOOL_AUTH_H */
