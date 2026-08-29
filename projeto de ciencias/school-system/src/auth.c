/* auth.c
 * Autenticação por telefone com sessões em memória.
 *
 * Para uma feira de ciências, manter sessões em memória é aceitável.
 * Em produção, usar Redis/JWT assinado.
 */
#include "auth.h"
#include "database.h"
#include "users.h"
#include "validation.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#define AUTH_MAX_SESSIONS 1024

static struct {
    char         token[65];
    AuthSession  s;
    long         expires_at;
    int          used;
} g_sessions[AUTH_MAX_SESSIONS];

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static int             g_timeout_min = 60;

void auth_shutdown(void)
{
    pthread_mutex_lock(&g_mu);
    memset(g_sessions, 0, sizeof g_sessions);
    pthread_mutex_unlock(&g_mu);
}

static void random_token_hex(char *out, size_t cap)
{
    uint8_t buf[32];
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        (void)!fread(buf, 1, sizeof buf, f);
        fclose(f);
    } else {
        /* Fallback: mistura várias fontes. Em Linux/macOS isto é
         * apenas defensivo; em Windows WSL ou containers restritos
         * ainda gera entropia suficiente para o token ser único. */
        static uint64_t seed = 0;
        if (seed == 0) seed = (uint64_t)time(NULL);
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL; /* LCG */
        uint64_t v = seed ^ ((uint64_t)(uintptr_t)out << 17);
        for (size_t i = 0; i < sizeof buf; i++) {
            v = v * 2862933555777941757ULL + 3037000493ULL;
            buf[i] = (uint8_t)(v >> 56);
        }
    }
    static const char *h = "0123456789abcdef";
    size_t need = sizeof buf * 2;
    if (need + 1 > cap) need = cap - 1;
    for (size_t i = 0; i < need; i++) {
        out[i] = h[(buf[i/2] >> ((1 - (i%2))*4)) & 0x0f];
    }
    out[need] = '\0';
}

int auth_login_phone(const char *phone, const char *password_or_null,
                     AuthSession *out)
{
    if (!phone || !validation_is_valid_phone(phone)) return ERROR_INVALID_PHONE;

    User u;
    int rc = users_find_by_phone(phone, &u);
    if (rc != SUCCESS) return ERROR_UNAUTHORIZED;
    if (u.status != USER_STATUS_ACTIVE) return ERROR_FORBIDDEN;

    sqlite3_stmt *st = NULL;
    rc = db_prepare("SELECT password_hash FROM users WHERE id=?;", &st);
    if (rc != SUCCESS) return rc;
    db_bind_int64(st, 1, (long long)u.id);
    rc = sqlite3_step(st);
    if (rc != SQLITE_ROW) { sqlite3_finalize(st); return ERROR_INTERNAL; }
    const unsigned char *h = sqlite3_column_text(st, 0);
    if (h && (!password_or_null || !*password_or_null ||
              users_verify_password(password_or_null, (const char *)h) != SUCCESS)) {
        sqlite3_finalize(st);
        return ERROR_UNAUTHORIZED;
    }
    sqlite3_finalize(st);

    if (out) {
        out->user_id = u.id;
        snprintf(out->phone, sizeof out->phone, "%s", u.phone);
        out->type = u.type;
        out->expires_at = time(NULL) + g_timeout_min * 60;
    }
    return SUCCESS;
}

int auth_session_create(AuthSession *s, char *token_out, size_t cap)
{
    if (!s || !token_out || cap < 65) return ERROR_INVALID_INPUT;
    char token[65];
    random_token_hex(token, sizeof token);

    pthread_mutex_lock(&g_mu);
    int slot = -1;
    for (int i = 0; i < AUTH_MAX_SESSIONS; i++) {
        if (!g_sessions[i].used) { slot = i; break; }
    }
    if (slot < 0) {
        pthread_mutex_unlock(&g_mu);
        return ERROR_INTERNAL;
    }
    g_sessions[slot].used = 1;
    snprintf(g_sessions[slot].token, sizeof g_sessions[slot].token, "%s", token);
    g_sessions[slot].s = *s;
    g_sessions[slot].expires_at = s->expires_at;
    pthread_mutex_unlock(&g_mu);

    snprintf(token_out, cap, "%s", token);
    return SUCCESS;
}

int auth_session_resolve(const char *token, AuthSession *out)
{
    if (!token) return ERROR_UNAUTHORIZED;
    pthread_mutex_lock(&g_mu);
    int rc = ERROR_UNAUTHORIZED;
    long now = time(NULL);
    for (int i = 0; i < AUTH_MAX_SESSIONS; i++) {
        if (g_sessions[i].used && strcmp(g_sessions[i].token, token) == 0) {
            if (g_sessions[i].expires_at > now) {
                if (out) *out = g_sessions[i].s;
                rc = SUCCESS;
            } else {
                rc = ERROR_SESSION_EXPIRED;
            }
            break;
        }
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

int auth_session_revoke(const char *token)
{
    if (!token) return ERROR_INVALID_INPUT;
    pthread_mutex_lock(&g_mu);
    int rc = ERROR_NOT_FOUND;
    for (int i = 0; i < AUTH_MAX_SESSIONS; i++) {
        if (g_sessions[i].used && strcmp(g_sessions[i].token, token) == 0) {
            memset(&g_sessions[i], 0, sizeof g_sessions[i]);
            rc = SUCCESS;
            break;
        }
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}
