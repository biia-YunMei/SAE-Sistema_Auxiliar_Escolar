/* Autenticacao, sessao opaca e protecao basica contra tentativas repetidas. */
#include "auth.h"
#include "database.h"
#include "users.h"
#include "validation.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(HAVE_OPENSSL_SHA)
#include <openssl/rand.h>
#endif

#define AUTH_MAX_SESSIONS 1024
#define LOGIN_MAX_ATTEMPTS 5
#define LOGIN_WINDOW_SECONDS (15 * 60)
#define LOGIN_LOCK_SECONDS (15 * 60)
#define AUTH_MAX_LOGIN_GUARDS 2048

typedef struct {
    char token[65];
    AuthSession session;
    long expires_at;
    int used;
} SessionSlot;

typedef struct {
    char phone[32];
    int attempts;
    long window_started;
    long locked_until;
} LoginGuard;

static SessionSlot g_sessions[AUTH_MAX_SESSIONS];
static LoginGuard g_guards[AUTH_MAX_LOGIN_GUARDS];
static pthread_mutex_t g_auth_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_timeout_min = 60;

void auth_set_session_timeout(int minutes)
{
    if (minutes >= 1 && minutes <= 1440) g_timeout_min = minutes;
}

void auth_shutdown(void)
{
    pthread_mutex_lock(&g_auth_mutex);
    memset(g_sessions, 0, sizeof g_sessions);
    memset(g_guards, 0, sizeof g_guards);
    pthread_mutex_unlock(&g_auth_mutex);
}

static int random_token(char out[65])
{
    unsigned char bytes[32];
#if defined(HAVE_OPENSSL_SHA)
    if (RAND_bytes(bytes, (int)sizeof bytes) != 1) return ERROR_INTERNAL;
#else
    FILE *random = fopen("/dev/urandom", "rb");
    if (!random || fread(bytes, 1, sizeof bytes, random) != sizeof bytes) {
        if (random) fclose(random);
        return ERROR_INTERNAL;
    }
    fclose(random);
#endif
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof bytes; ++i) {
        out[i * 2] = hex[bytes[i] >> 4];
        out[i * 2 + 1] = hex[bytes[i] & 0x0f];
    }
    out[64] = '\0';
    return SUCCESS;
}

static int guard_slot(const char *phone, long now)
{
    int free_slot = -1;
    for (int i = 0; i < AUTH_MAX_LOGIN_GUARDS; ++i) {
        if (g_guards[i].phone[0] && g_guards[i].locked_until <= now &&
            now - g_guards[i].window_started > LOGIN_WINDOW_SECONDS)
            memset(&g_guards[i], 0, sizeof g_guards[i]);
        if (g_guards[i].phone[0] == '\0' && free_slot < 0) free_slot = i;
        if (strcmp(g_guards[i].phone, phone) == 0) return i;
    }
    int slot = free_slot;
    if (slot < 0) {
        long oldest = now;
        for (int i = 0; i < AUTH_MAX_LOGIN_GUARDS; ++i) {
            if (g_guards[i].locked_until > now) continue;
            if (slot < 0 || g_guards[i].window_started < oldest) {
                slot = i;
                oldest = g_guards[i].window_started;
            }
        }
    }
    if (slot >= 0) {
        memset(&g_guards[slot], 0, sizeof g_guards[slot]);
        snprintf(g_guards[slot].phone, sizeof g_guards[slot].phone, "%s", phone);
        g_guards[slot].window_started = now;
    }
    return slot;
}

static int login_is_allowed(const char *phone)
{
    long now = time(NULL);
    pthread_mutex_lock(&g_auth_mutex);
    int slot = guard_slot(phone, now);
    int allowed = slot >= 0 && g_guards[slot].locked_until <= now;
    pthread_mutex_unlock(&g_auth_mutex);
    return allowed;
}

static void register_login_result(const char *phone, int success)
{
    long now = time(NULL);
    pthread_mutex_lock(&g_auth_mutex);
    int slot = guard_slot(phone, now);
    if (slot >= 0) {
        if (success) {
            memset(&g_guards[slot], 0, sizeof g_guards[slot]);
        } else {
            if (now - g_guards[slot].window_started > LOGIN_WINDOW_SECONDS) {
                g_guards[slot].attempts = 0;
                g_guards[slot].window_started = now;
            }
            if (++g_guards[slot].attempts >= LOGIN_MAX_ATTEMPTS)
                g_guards[slot].locked_until = now + LOGIN_LOCK_SECONDS;
        }
    }
    pthread_mutex_unlock(&g_auth_mutex);
}

int auth_login_phone_from_ip(const char *phone, const char *password,
                             const char *client_ip, AuthSession *out)
{
    if (!phone || !validation_is_valid_phone(phone)) return ERROR_INVALID_PHONE;
    if (client_ip && *client_ip && !login_is_allowed(client_ip))
        return ERROR_FORBIDDEN;
    if (!login_is_allowed(phone)) return ERROR_FORBIDDEN;

    User user;
    if (users_find_by_phone(phone, &user) != SUCCESS || user.status != USER_STATUS_ACTIVE) {
        register_login_result(phone, 0);
        if (client_ip && *client_ip) register_login_result(client_ip, 0);
        return ERROR_UNAUTHORIZED;
    }

    /* Alunos não possuem acesso por login; seus dados são apenas cadastrais. */
    if (user.type == USER_TYPE_STUD) {
        register_login_result(phone, 0);
        if (client_ip && *client_ip) register_login_result(client_ip, 0);
        return ERROR_UNAUTHORIZED;
    }

    sqlite3_stmt *statement = NULL;
    if (db_prepare("SELECT password_hash FROM users WHERE id=?;", &statement) != SUCCESS)
        return ERROR_DATABASE;
    if (db_bind_int64(statement, 1, (long long)user.id) != SUCCESS) {
        sqlite3_finalize(statement);
        return ERROR_DATABASE;
    }
    int found = sqlite3_step(statement) == SQLITE_ROW;
    const unsigned char *hash = found ? sqlite3_column_text(statement, 0) : NULL;
    /* O telefone identifica a conta; a senha comprova a posse da conta tanto
     * para professores quanto para coordenação. Alunos não entram no site. */
    char old_hash[128] = {0};
    int legacy_hash = found && hash && strchr((const char *)hash, ':') != NULL &&
        strlen((const char *)hash) < sizeof old_hash;
    if (legacy_hash) snprintf(old_hash, sizeof old_hash, "%s", (const char *)hash);
    int valid = found && hash && password && *password &&
        users_verify_password(password, (const char *)hash) == SUCCESS;
    sqlite3_finalize(statement);
    if (!valid) {
        register_login_result(phone, 0);
        if (client_ip && *client_ip) register_login_result(client_ip, 0);
        return ERROR_UNAUTHORIZED;
    }

    /* Migração transparente dos hashes SHA-256 antigos após autenticação
     * válida. A conta continua protegida caso a atualização falhe. */
    if (legacy_hash) {
        char upgraded[128];
        sqlite3_stmt *update = NULL;
        if (users_hash_password(password, upgraded, sizeof upgraded) == SUCCESS &&
            db_prepare("UPDATE users SET password_hash=? WHERE id=? AND password_hash=?;", &update) == SUCCESS) {
            db_bind_text(update, 1, upgraded);
            db_bind_int64(update, 2, (long long)user.id);
            db_bind_text(update, 3, old_hash);
            (void)sqlite3_step(update);
            sqlite3_finalize(update);
        }
    }

    if (out) {
        out->user_id = user.id;
        out->type = user.type;
        snprintf(out->phone, sizeof out->phone, "%s", user.phone);
        out->expires_at = time(NULL) + g_timeout_min * 60;
    }
    register_login_result(phone, 1);
    if (client_ip && *client_ip) register_login_result(client_ip, 1);
    return SUCCESS;
}

int auth_login_phone(const char *phone, const char *password, AuthSession *out)
{
    return auth_login_phone_from_ip(phone, password, NULL, out);
}

int auth_session_create(AuthSession *session, char *token_out, size_t cap)
{
    if (!session || !token_out || cap < 65) return ERROR_INVALID_INPUT;
    char token[65];
    if (random_token(token) != SUCCESS) return ERROR_INTERNAL;

    pthread_mutex_lock(&g_auth_mutex);
    long now = time(NULL);
    int slot = -1;
    for (int i = 0; i < AUTH_MAX_SESSIONS; ++i) {
        if (g_sessions[i].used && g_sessions[i].expires_at <= now)
            memset(&g_sessions[i], 0, sizeof g_sessions[i]);
        if (!g_sessions[i].used && slot < 0) slot = i;
    }
    if (slot >= 0) {
        g_sessions[slot].used = 1;
        g_sessions[slot].session = *session;
        g_sessions[slot].expires_at = session->expires_at;
        snprintf(g_sessions[slot].token, sizeof g_sessions[slot].token, "%s", token);
    }
    pthread_mutex_unlock(&g_auth_mutex);
    if (slot < 0) return ERROR_INTERNAL;
    snprintf(token_out, cap, "%s", token);
    return SUCCESS;
}

int auth_session_resolve(const char *token, AuthSession *out)
{
    if (!token || strlen(token) != 64) return ERROR_UNAUTHORIZED;
    pthread_mutex_lock(&g_auth_mutex);
    long now = time(NULL);
    int result = ERROR_UNAUTHORIZED;
    for (int i = 0; i < AUTH_MAX_SESSIONS; ++i) {
        if (!g_sessions[i].used || strcmp(g_sessions[i].token, token) != 0) continue;
        if (g_sessions[i].expires_at <= now) {
            memset(&g_sessions[i], 0, sizeof g_sessions[i]);
            result = ERROR_SESSION_EXPIRED;
        } else {
            if (out) *out = g_sessions[i].session;
            result = SUCCESS;
        }
        break;
    }
    pthread_mutex_unlock(&g_auth_mutex);
    if (result == SUCCESS) {
        AuthSession snapshot;
        if (out) snapshot = *out;
        else {
            /* O token continua válido somente enquanto a conta existir e
             * estiver autorizada no cadastro. */
            pthread_mutex_lock(&g_auth_mutex);
            int found = 0;
            for (int i = 0; i < AUTH_MAX_SESSIONS; ++i)
                if (g_sessions[i].used && strcmp(g_sessions[i].token, token) == 0) {
                    snapshot = g_sessions[i].session; found = 1; break;
                }
            pthread_mutex_unlock(&g_auth_mutex);
            if (!found) return ERROR_UNAUTHORIZED;
        }
        User current;
        if (users_find_by_id(snapshot.user_id, &current) != SUCCESS ||
            current.status != USER_STATUS_ACTIVE || current.type != snapshot.type) {
            (void)auth_session_revoke(token);
            return ERROR_UNAUTHORIZED;
        }
        if (out) *out = snapshot;
    }
    return result;
}

int auth_session_revoke(const char *token)
{
    if (!token) return ERROR_INVALID_INPUT;
    pthread_mutex_lock(&g_auth_mutex);
    int result = ERROR_NOT_FOUND;
    for (int i = 0; i < AUTH_MAX_SESSIONS; ++i) {
        if (g_sessions[i].used && strcmp(g_sessions[i].token, token) == 0) {
            memset(&g_sessions[i], 0, sizeof g_sessions[i]);
            result = SUCCESS;
            break;
        }
    }
    pthread_mutex_unlock(&g_auth_mutex);
    return result;
}

void auth_session_revoke_user(int64_t user_id)
{
    if (user_id <= 0) return;
    pthread_mutex_lock(&g_auth_mutex);
    for (int i = 0; i < AUTH_MAX_SESSIONS; ++i) {
        if (g_sessions[i].used && g_sessions[i].session.user_id == user_id)
            memset(&g_sessions[i], 0, sizeof g_sessions[i]);
    }
    pthread_mutex_unlock(&g_auth_mutex);
}
