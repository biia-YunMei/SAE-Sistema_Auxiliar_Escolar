/* users.h
 * Modelo de usuário.
 *
 *   COORD → coordenador (administrador)
 *   PROF  → professor
 *   STUD  → aluno
 *
 * Senhas (quando existirem) são armazenadas com hash SHA-256 + salt.
 * Para uma feira de ciências, isso já demonstra o cuidado; o README
 * indica a evolução para bcrypt/argon2.
 */
#ifndef SCHOOL_USERS_H
#define SCHOOL_USERS_H

#include "error.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    USER_TYPE_INVALID = 0,
    USER_TYPE_COORD,
    USER_TYPE_PROF,
    USER_TYPE_STUD
} UserType;

typedef enum {
    USER_STATUS_ACTIVE   = 0,
    USER_STATUS_INACTIVE = 1,
    USER_STATUS_BLOCKED  = 2
} UserStatus;

typedef struct {
    int64_t     id;
    char        name[121];
    char        phone[32];
    UserType    type;
    UserStatus  status;
    char        created_at[32];
} User;

const char *user_type_str (UserType t);
const char *user_status_str(UserStatus s);
UserType    user_type_from(const char *s);

/* CRUD de usuários (validação, prepared statements, hash de senha). */
int users_create(const char *name, const char *phone,
                 UserType type, const char *plain_password, /* NULL ok */
                 User *out);

int users_find_by_phone(const char *phone, User *out);
int users_find_by_id(int64_t id, User *out);
int users_set_status(int64_t id, UserStatus status);

/* Hash de senha. O salt é gerado internamente. */
int users_hash_password(const char *plain, char *out_hex, size_t cap);
int users_verify_password(const char *plain, const char *hash_hex);

#endif /* SCHOOL_USERS_H */
