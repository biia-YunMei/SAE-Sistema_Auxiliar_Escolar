/* users.c: CRUD de usuários, hashes PBKDF2 para novas senhas e verificação
 * dos hashes SHA-256 legados durante a migração transparente de contas. */
#include "users.h"
#include "database.h"
#include "validation.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <limits.h>
#include <time.h>

/* OpenSSL fornece PBKDF2 e aleatoriedade para hashes novos. O SHA-256 próprio
 * abaixo só verifica hashes antigos em builds sem OpenSSL. */
#if defined(HAVE_OPENSSL_SHA)
#  include <openssl/sha.h>
#  include <openssl/rand.h>
#  include <openssl/evp.h>
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

static int from_hex(const char *src, uint8_t *dst, size_t bytes)
{
    if (!src || !dst || strlen(src) != bytes * 2) return 0;
    for (size_t i = 0; i < bytes; ++i) {
        unsigned int value = 0;
        if (sscanf(src + i * 2, "%2x", &value) != 1) return 0;
        dst[i] = (uint8_t)value;
    }
    return 1;
}

static int random_salt_hex(char *out)
{
    /* 16 bytes -> 32 hex chars. */
    uint8_t salt[16];
#if defined(HAVE_OPENSSL_SHA)
    if (RAND_bytes(salt, (int)sizeof salt) != 1) return ERROR_INTERNAL;
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return ERROR_INTERNAL;
    size_t read_count = fread(salt, 1, sizeof salt, f);
    fclose(f);
    if (read_count != sizeof salt) return ERROR_INTERNAL;
#endif
    to_hex(salt, sizeof salt, out);
    return SUCCESS;
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
    if (!plain || !out_hex || cap < 119) return ERROR_INVALID_INPUT;
    if (!validation_length_between(plain, 8, 127)) return ERROR_INVALID_INPUT;
#if !defined(HAVE_OPENSSL_SHA)
    (void)plain; (void)out_hex; (void)cap;
    return ERROR_CONFIG;
#else
    char salt[33];
    if (random_salt_hex(salt) != SUCCESS) return ERROR_INTERNAL;
    if (strlen(plain) > INT_MAX) return ERROR_TOO_LONG;
    uint8_t salt_bytes[16];
    if (!from_hex(salt, salt_bytes, sizeof salt_bytes)) return ERROR_INTERNAL;
    uint8_t digest[32];
    const int iterations = 210000;
    if (PKCS5_PBKDF2_HMAC(plain, (int)strlen(plain), salt_bytes,
            (int)sizeof salt_bytes, iterations, EVP_sha256(),
            (int)sizeof digest, digest) != 1) return ERROR_INTERNAL;
    char hex[65];
    to_hex(digest, sizeof digest, hex);
    int w = snprintf(out_hex, cap, "pbkdf2-sha256$%d$%s$%s",
                     iterations, salt, hex);
    if (w < 0 || (size_t)w >= cap) return ERROR_TOO_LONG;
    return SUCCESS;
#endif
}

int users_verify_password(const char *plain, const char *stored)
{
    if (!plain || !stored) return ERROR_UNAUTHORIZED;
    if (strncmp(stored, "pbkdf2-sha256$", 14) == 0) {
#if !defined(HAVE_OPENSSL_SHA)
        return ERROR_CONFIG;
#else
        const char *iterations_text = stored + 14;
        char *end = NULL;
        long iterations = strtol(iterations_text, &end, 10);
        if (!end || *end != '$' || iterations < 100000 || iterations > 1000000)
            return ERROR_UNAUTHORIZED;
        const char *salt_hex = end + 1;
        const char *digest_hex = strchr(salt_hex, '$');
        if (!digest_hex || (size_t)(digest_hex - salt_hex) != 32 ||
            strlen(digest_hex + 1) != 64 || strlen(plain) > INT_MAX)
            return ERROR_UNAUTHORIZED;
        char salt_text[33]; uint8_t salt[16], expected[32], actual[32];
        memcpy(salt_text, salt_hex, 32); salt_text[32] = '\0';
        if (!from_hex(salt_text, salt, sizeof salt) ||
            !from_hex(digest_hex + 1, expected, sizeof expected) ||
            PKCS5_PBKDF2_HMAC(plain, (int)strlen(plain), salt, (int)sizeof salt,
                (int)iterations, EVP_sha256(), (int)sizeof actual, actual) != 1)
            return ERROR_UNAUTHORIZED;
        unsigned int diff = 0;
        for (size_t i = 0; i < sizeof actual; ++i) diff |= actual[i] ^ expected[i];
        return diff == 0 ? SUCCESS : ERROR_UNAUTHORIZED;
#endif
    }
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
    if (strlen(sep + 1) != 64) return ERROR_UNAUTHORIZED;
    unsigned int diff = 0;
    for (size_t i = 0; i < sizeof digest; ++i) {
        unsigned int expected = 0;
        if (sscanf(sep + 1 + i * 2, "%2x", &expected) != 1)
            return ERROR_UNAUTHORIZED;
        diff |= digest[i] ^ (uint8_t)expected;
    }
    return diff == 0 ? SUCCESS : ERROR_UNAUTHORIZED;
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

    int own_transaction = sqlite3_get_autocommit(database_get()) != 0;
    if (own_transaction && db_begin() != SUCCESS) return ERROR_DATABASE;

    sqlite3_stmt *st = NULL;
    int rc = db_prepare(
        "INSERT INTO users(name, phone, user_type, password_hash) "
        "VALUES(?,?,?,?);", &st);
    if (rc != SUCCESS) goto fail;

    rc = db_bind_text(st, 1, name);
    if (rc == SUCCESS) rc = db_bind_text(st, 2, phone);
    if (rc == SUCCESS) rc = db_bind_text(st, 3, user_type_str(type));
    if (rc == SUCCESS) rc = plain_password ? db_bind_text(st, 4, hash) : db_bind_null(st, 4);
    if (rc != SUCCESS) { sqlite3_finalize(st); goto fail; }

    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) {
        rc = rc == SQLITE_CONSTRAINT ? ERROR_ALREADY_EXISTS : ERROR_DATABASE;
        goto fail;
    }
    rc = users_find_by_phone(phone, out);
    if (rc != SUCCESS) goto fail;

    /* Estes perfis são necessários para vínculos de turmas e tarefas.
     * Criá-los aqui garante o mesmo resultado para API, CLI e testes. */
    if (type == USER_TYPE_PROF || type == USER_TYPE_STUD) {
        const char *sql = type == USER_TYPE_PROF
            ? "INSERT INTO teachers(user_id) VALUES(?);"
            : "INSERT INTO students(user_id) VALUES(?);";
        st = NULL;
        rc = db_prepare(sql, &st);
        if (rc != SUCCESS) goto fail;
        db_bind_int64(st, 1, (long long)out->id);
        rc = sqlite3_step(st);
        sqlite3_finalize(st);
        if (rc != SQLITE_DONE) { rc = ERROR_DATABASE; goto fail; }
    }
    if (own_transaction && db_commit() != SUCCESS) {
        (void)db_rollback();
        return ERROR_DATABASE;
    }
    return SUCCESS;

fail:
    if (own_transaction) (void)db_rollback();
    return rc;
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
