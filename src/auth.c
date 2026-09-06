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
static LoginGuard g_guards[AUTH_MAX_SESSIONS];
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
    for (int i = 0; i < AUTH_MAX_SESSIONS; ++i) {
        if (g_guards[i].phone[0] == '\0' && free_slot < 0) free_slot = i;
        if (strcmp(g_guards[i].phone, phone) == 0) return i;
    }
    if (free_slot >= 0) {
        snprintf(g_guards[free_slot].phone, sizeof g_guards[free_slot].phone, "%s", phone);
        g_guards[free_slot].window_started = now;
    }
    return free_slot;
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

int auth_login_phone(const char *phone, const char *password, AuthSession *out)
{
    if (!phone || !validation_is_valid_phone(phone)) return ERROR_INVALID_PHONE;
    if (!login_is_allowed(phone)) return ERROR_FORBIDDEN;

    User user;
    if (users_find_by_phone(phone, &user) != SUCCESS || user.status != USER_STATUS_ACTIVE) {
        register_login_result(phone, 0);
        return ERROR_UNAUTHORIZED;
    }

    sqlite3_stmt *statement = NULL;
    if (db_prepare("SELECT password_hash FROM users WHERE id=?;", &statement) != SUCCESS)
        return ERROR_DATABASE;
    db_bind_int64(statement, 1, (long long)user.id);
    int found = sqlite3_step(statement) == SQLITE_ROW;
    const unsigned char *hash = found ? sqlite3_column_text(statement, 0) : NULL;
    int valid = found && hash && password && *password &&
                users_verify_password(password, (const char *)hash) == SUCCESS;
    sqlite3_finalize(statement);
    if (!valid) {
        register_login_result(phone, 0);
        return ERROR_UNAUTHORIZED;
    }

    if (out) {
        out->user_id = user.id;
        out->type = user.type;
        snprintf(out->phone, sizeof out->phone, "%s", user.phone);
        out->expires_at = time(NULL) + g_timeout_min * 60;
    }
    register_login_result(phone, 1);
    return SUCCESS;
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
