/* users.c
 * Implementação do CRUD de usuários.
 *
 * Senhas: SHA-256 com salt embutido no formato "salt:hash" em hex.
 * Para uma feira de ciências isso é suficiente. Em produção, usar
 * bcrypt/argon2 (apontado no README como evolução).
 */
#include "users.h"
#include "database.h"
#include "validation.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <time.h>

/* OpenSSL é opcional. Se disponível, use SHA256 acelerado.
 * Caso contrário, ativamos o fallback abaixo com nossa própria
 * implementação de SHA-256 (didática, suficiente para a feira). */
#if defined(HAVE_OPENSSL_SHA)
#  include <openssl/sha.h>
#endif

#ifndef HAVE_OPENSSL_SHA
/* Fallback SHA-256 minimalista (apenas para o projeto educacional).
 * NÃO use em produção: prefira OpenSSL/BoringSSL. */
typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  buffer[64];
} sha256_ctx;
static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
static inline uint32_t rotr(uint32_t x, int n) { return (x>>n)|(x<<(32-n)); }
static void sha256_transform(sha256_ctx *c, const uint8_t *d) {
    uint32_t w[64];
    for (int i=0;i<16;i++)
        w[i] = ((uint32_t)d[i*4]<<24)|((uint32_t)d[i*4+1]<<16)|
               ((uint32_t)d[i*4+2]<<8)|(uint32_t)d[i*4+3];
    for (int i=16;i<64;i++) {
        uint32_t s0 = rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3);
        uint32_t s1 = rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=c->state[0],b=c->state[1],cc=c->state[2],d2=c->state[3];
    uint32_t e=c->state[4],f=c->state[5],g=c->state[6],h=c->state[7];
    for (int i=0;i<64;i++) {
        uint32_t S1 = rotr(e,6)^rotr(e,11)^rotr(e,25);
        uint32_t ch = (e&f)^(~e&g);
        uint32_t t1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = rotr(a,2)^rotr(a,13)^rotr(a,22);
        uint32_t mj = (a&b)^(a&cc)^(b&cc);
        uint32_t t2 = S0 + mj;
        h=g; g=f; f=e; e=d2+t1; d2=cc; cc=b; b=a; a=t1+t2;
    }
    c->state[0]+=a; c->state[1]+=b; c->state[2]+=cc; c->state[3]+=d2;
    c->state[4]+=e; c->state[5]+=f; c->state[6]+=g; c->state[7]+=h;
}
static void sha256_init(sha256_ctx *c) {
    c->state[0]=0x6a09e667; c->state[1]=0xbb67ae85; c->state[2]=0x3c6ef372; c->state[3]=0xa54ff53a;
    c->state[4]=0x510e527f; c->state[5]=0x9b05688c; c->state[6]=0x1f83d9ab; c->state[7]=0x5be0cd19;
    c->bitlen = 0;
}
static void sha256_update(sha256_ctx *c, const uint8_t *d, size_t len) {
    while (len > 0) {
        size_t fill = (c->bitlen/8) & 63;
        size_t take = 64 - fill;
        if (take > len) take = len;
        memcpy(c->buffer + fill, d, take);
        c->bitlen += take*8;
        d += take; len -= take;
        if (((c->bitlen/8)&63) == 0) sha256_transform(c, c->buffer);
    }
}
static void sha256_final(sha256_ctx *c, uint8_t out[32]) {
    size_t used = (c->bitlen/8) & 63;
    c->buffer[used++] = 0x80;
    if (used > 56) { while (used<64) c->buffer[used++]=0; sha256_transform(c,c->buffer); used=0; }
    while (used<56) c->buffer[used++]=0;
    uint64_t bl = c->bitlen;
    for (int i=7;i>=0;i--) c->buffer[56+i] = (uint8_t)(bl>>(i*8));
    sha256_transform(c, c->buffer);
    for (int i=0;i<8;i++) {
        out[i*4]   = (uint8_t)(c->state[i]>>24);
        out[i*4+1] = (uint8_t)(c->state[i]>>16);
        out[i*4+2] = (uint8_t)(c->state[i]>>8);
        out[i*4+3] = (uint8_t)(c->state[i]);
    }
}
#endif /* HAVE_OPENSSL_SHA */

static void to_hex(const uint8_t *src, size_t n, char *dst)
{
    static const char *h = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        dst[i*2]   = h[src[i] >> 4];
        dst[i*2+1] = h[src[i] & 0x0f];
    }
    dst[n*2] = '\0';
}

static void random_salt_hex(char *out, size_t hex_len)
{
    /* 16 bytes -> 32 hex chars. */
    uint8_t salt[16];
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        (void)!fread(salt, 1, sizeof salt, f);
        fclose(f);
    } else {
        /* Fallback LCG (mesmo do auth.c): só roda em sistemas
         * sem /dev/urandom, o que é raro em Linux/macOS. */
        static uint64_t seed = 0;
        if (seed == 0) seed = (uint64_t)time(NULL);
        uint64_t v = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        for (size_t i = 0; i < sizeof salt; i++) {
            v = v * 2862933555777941757ULL + 3037000493ULL;
            salt[i] = (uint8_t)(v >> 56);
        }
    }
    to_hex(salt, sizeof salt, out);
    (void)hex_len;
}

/* ===================== helpers =================================== */

const char *user_type_str(UserType t)
{
    switch (t) {
    case USER_TYPE_COORD: return "COORD";
    case USER_TYPE_PROF:  return "PROF";
    case USER_TYPE_STUD:  return "STUD";
    default: return "INVALID";
    }
}

const char *user_status_str(UserStatus s)
{
    switch (s) {
    case USER_STATUS_ACTIVE:   return "ACTIVE";
    case USER_STATUS_INACTIVE: return "INACTIVE";
    case USER_STATUS_BLOCKED:  return "BLOCKED";
    }
    return "UNKNOWN";
}

UserType user_type_from(const char *s)
{
    if (!s) return USER_TYPE_INVALID;
    if (strcmp(s, "COORD") == 0) return USER_TYPE_COORD;
    if (strcmp(s, "PROF")  == 0) return USER_TYPE_PROF;
    if (strcmp(s, "STUD")  == 0) return USER_TYPE_STUD;
    return USER_TYPE_INVALID;
}

int users_hash_password(const char *plain, char *out_hex, size_t cap)
{
    if (!plain || !out_hex || cap < 80) return ERROR_INVALID_INPUT;
    char salt[33];
    random_salt_hex(salt, sizeof salt);

    char buf[256];
    int n = snprintf(buf, sizeof buf, "%s|%s", salt, plain);
    if (n < 0 || (size_t)n >= sizeof buf) return ERROR_TOO_LONG;

    uint8_t digest[32];
#ifdef HAVE_OPENSSL_SHA
    SHA256((const unsigned char *)buf, (size_t)n, digest);
#else
    sha256_ctx c; sha256_init(&c);
    sha256_update(&c, (const uint8_t *)buf, (size_t)n);
    sha256_final(&c, digest);
#endif
    char hex[65];
    to_hex(digest, sizeof digest, hex);

    int w = snprintf(out_hex, cap, "%s:%s", salt, hex);
    if (w < 0 || (size_t)w >= cap) return ERROR_TOO_LONG;
    return SUCCESS;
}

int users_verify_password(const char *plain, const char *stored)
{
    if (!plain || !stored) return ERROR_UNAUTHORIZED;
    const char *sep = strchr(stored, ':');
    if (!sep) return ERROR_UNAUTHORIZED;
    size_t slen = (size_t)(sep - stored);
    if (slen == 0 || slen >= 33) return ERROR_UNAUTHORIZED;

    char buf[256];
    int n = snprintf(buf, sizeof buf, "%.*s|%s",
                     (int)slen, stored, plain);
    if (n < 0 || (size_t)n >= sizeof buf) return ERROR_TOO_LONG;

    uint8_t digest[32];
#ifdef HAVE_OPENSSL_SHA
    SHA256((const unsigned char *)buf, (size_t)n, digest);
#else
    sha256_ctx c; sha256_init(&c);
    sha256_update(&c, (const uint8_t *)buf, (size_t)n);
    sha256_final(&c, digest);
#endif
    char hex[65];
    to_hex(digest, sizeof digest, hex);
    if (strcmp(hex, sep + 1) != 0) return ERROR_UNAUTHORIZED;
    return SUCCESS;
}

/* ===================== CRUD ====================================== */

static int fetch_user_by_sql(const char *sql, const char *param, User *out)
{
    sqlite3_stmt *st = NULL;
    int rc = db_prepare(sql, &st);
    if (rc != SUCCESS) return rc;
    if (param) {
        rc = db_bind_text(st, 1, param);
        if (rc != SUCCESS) { sqlite3_finalize(st); return rc; }
    }
    rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        if (out) {
            out->id     = sqlite3_column_int64(st, 0);
            const unsigned char *nm = sqlite3_column_text(st, 1);
            const unsigned char *ph = sqlite3_column_text(st, 2);
            const unsigned char *tp = sqlite3_column_text(st, 3);
            const unsigned char *stt= sqlite3_column_text(st, 4);
            const unsigned char *cr = sqlite3_column_text(st, 5);
            snprintf(out->name, sizeof out->name, "%s", nm ? (const char *)nm : "");
            snprintf(out->phone,sizeof out->phone,"%s", ph ? (const char *)ph : "");
            out->type   = user_type_from(tp ? (const char *)tp : "");
            out->status = strcmp((const char *)(stt?stt:(const unsigned char*)""), "ACTIVE") == 0
                          ? USER_STATUS_ACTIVE
                          : (strcmp((const char *)(stt?stt:(const unsigned char*)""), "INACTIVE") == 0
                             ? USER_STATUS_INACTIVE : USER_STATUS_BLOCKED);
            snprintf(out->created_at, sizeof out->created_at, "%s", cr ? (const char *)cr : "");
        }
        sqlite3_finalize(st);
        return SUCCESS;
    }
    sqlite3_finalize(st);
    return ERROR_NOT_FOUND;
}

int users_create(const char *name, const char *phone,
                 UserType type, const char *plain_password, User *out)
{
    if (!name || !phone || type == USER_TYPE_INVALID) return ERROR_INVALID_INPUT;
    if (!validation_length_between(name, 1, 120)) return ERROR_TOO_LONG;
    if (!validation_is_valid_phone(phone))         return ERROR_INVALID_PHONE;

    char hash[128] = {0};
    if (plain_password) {
        int rc = users_hash_password(plain_password, hash, sizeof hash);
        if (rc != SUCCESS) return rc;
    }

    sqlite3_stmt *st = NULL;
    int rc = db_prepare(
        "INSERT INTO users(name, phone, user_type, password_hash) "
        "VALUES(?,?,?,?);", &st);
    if (rc != SUCCESS) return rc;

    rc = db_bind_text(st, 1, name);
    if (rc == SUCCESS) rc = db_bind_text(st, 2, phone);
    if (rc == SUCCESS) rc = db_bind_text(st, 3, user_type_str(type));
    if (rc == SUCCESS) rc = plain_password ? db_bind_text(st, 4, hash) : db_bind_null(st, 4);
    if (rc != SUCCESS) { sqlite3_finalize(st); return rc; }

    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) {
        if (rc == SQLITE_CONSTRAINT) return ERROR_ALREADY_EXISTS;
        return ERROR_DATABASE;
    }
    rc = users_find_by_phone(phone, out);
    if (rc != SUCCESS) return rc;

    /* Estes perfis são necessários para vínculos de turmas e tarefas.
     * Criá-los aqui garante o mesmo resultado para API, CLI e testes. */
    if (type == USER_TYPE_PROF || type == USER_TYPE_STUD) {
        const char *sql = type == USER_TYPE_PROF
            ? "INSERT INTO teachers(user_id) VALUES(?);"
            : "INSERT INTO students(user_id) VALUES(?);";
        st = NULL;
        rc = db_prepare(sql, &st);
        if (rc != SUCCESS) return rc;
        db_bind_int64(st, 1, (long long)out->id);
        rc = sqlite3_step(st);
        sqlite3_finalize(st);
        if (rc != SQLITE_DONE) return ERROR_DATABASE;
    }
    return SUCCESS;
}

int users_find_by_phone(const char *phone, User *out)
{
    return fetch_user_by_sql(
        "SELECT id, name, phone, user_type, status, created_at "
        "FROM users WHERE phone=? LIMIT 1;", phone, out);
}

int users_find_by_id(int64_t id, User *out)
{
    char buf[32]; snprintf(buf, sizeof buf, "%lld", (long long)id);
    return fetch_user_by_sql(
        "SELECT id, name, phone, user_type, status, created_at "
        "FROM users WHERE id=? LIMIT 1;", buf, out);
}

int users_update(int64_t id, const char *name, const char *phone,
                 UserType type, const char *plain_password_or_null, User *out)
{
    if (id <= 0 || !name || !phone || type == USER_TYPE_INVALID) return ERROR_INVALID_INPUT;
    if (!validation_length_between(name, 1, 120)) return ERROR_TOO_LONG;
    if (!validation_is_valid_phone(phone)) return ERROR_INVALID_PHONE;

    char hash[128] = {0};
    if (plain_password_or_null && *plain_password_or_null) {
        int rc = users_hash_password(plain_password_or_null, hash, sizeof hash);
        if (rc != SUCCESS) return rc;
    }

    sqlite3_stmt *st = NULL;
    int rc = db_prepare(
        "UPDATE users SET name=?, phone=?, user_type=?, "
        "password_hash=COALESCE(?, password_hash) WHERE id=?;", &st);
    if (rc != SUCCESS) return rc;
    db_bind_text(st, 1, name);
    db_bind_text(st, 2, phone);
    db_bind_text(st, 3, user_type_str(type));
    if (hash[0]) db_bind_text(st, 4, hash); else db_bind_null(st, 4);
    db_bind_int64(st, 5, (long long)id);
    rc = sqlite3_step(st);
    int changed = sqlite3_changes(database_get());
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return rc == SQLITE_CONSTRAINT ? ERROR_ALREADY_EXISTS : ERROR_DATABASE;
    if (changed == 0) return ERROR_NOT_FOUND;
    return users_find_by_id(id, out);
}

int users_delete(int64_t id)
{
    if (id <= 0) return ERROR_INVALID_INPUT;
    sqlite3_stmt *st = NULL;
    int rc = db_prepare("DELETE FROM users WHERE id=?;", &st);
    if (rc != SUCCESS) return rc;
    db_bind_int64(st, 1, (long long)id);
    rc = sqlite3_step(st);
    int changed = sqlite3_changes(database_get());
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return ERROR_DATABASE;
    return changed ? SUCCESS : ERROR_NOT_FOUND;
}

int users_set_status(int64_t id, UserStatus status)
{
    sqlite3_stmt *st = NULL;
    int rc = db_prepare(
        "UPDATE users SET status=? WHERE id=?;", &st);
    if (rc != SUCCESS) return rc;
    db_bind_text(st, 1, user_status_str(status));
    db_bind_int64(st, 2, (long long)id);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return ERROR_DATABASE;
    return SUCCESS;
}
