#include "anonymous_enrollment.h"
#include "classes.h"
#include "database.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#if defined(HAVE_OPENSSL_SHA)
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

static int normalize_code(const char *source, char out[17]);

static int enrollment_hash(const char *code, char out[65])
{
#if defined(HAVE_OPENSSL_SHA)
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    if (EVP_Digest(code, strlen(code), digest, &length, EVP_sha256(), NULL) != 1 ||
        length != 32) return ERROR_INTERNAL;
    static const char hex[] = "0123456789abcdef";
    for (unsigned int i = 0; i < length; ++i) {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 0x0f];
    }
    out[64] = '\0';
    return SUCCESS;
#else
    (void)code; (void)out;
    return ERROR_CONFIG;
#endif
}

int anonymous_teacher_link_create(int64_t teacher_user_id, char code_out[17])
{
    if (teacher_user_id <= 0 || !code_out) return ERROR_INVALID_INPUT;
#if !defined(HAVE_OPENSSL_SHA)
    return ERROR_CONFIG;
#else
    sqlite3_stmt *st = NULL;
    if (db_prepare("SELECT 1 FROM users u JOIN teachers t ON t.user_id=u.id WHERE u.id=? AND u.user_type='PROF' AND u.status='ACTIVE';", &st) != SUCCESS)
        return ERROR_DATABASE;
    db_bind_int64(st, 1, (long long)teacher_user_id);
    int valid = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
    if (!valid) return ERROR_FORBIDDEN;

    static const char alphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    unsigned char random[16]; char code[17], hash[65];
    if (RAND_bytes(random, (int)sizeof random) != 1) return ERROR_INTERNAL;
    for (size_t i = 0; i < sizeof random; ++i) code[i] = alphabet[random[i] & 31U];
    code[16] = '\0';
    if (enrollment_hash(code, hash) != SUCCESS) return ERROR_INTERNAL;
    (void)db_exec("DELETE FROM whatsapp_teacher_link_codes WHERE expires_at<=datetime('now') OR claimed_at<=datetime('now','-30 days');");
    if (db_begin() != SUCCESS) return ERROR_DATABASE;
    if (db_prepare("DELETE FROM whatsapp_teacher_link_codes WHERE user_id=? AND claimed_at IS NULL;", &st) != SUCCESS) {
        (void)db_rollback(); return ERROR_DATABASE;
    }
    db_bind_int64(st, 1, (long long)teacher_user_id);
    int deleted = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    if (!deleted) { (void)db_rollback(); return ERROR_DATABASE; }
    if (db_prepare("INSERT INTO whatsapp_teacher_link_codes(code_hash,user_id,expires_at) VALUES(?,?,datetime('now','+10 minutes'));", &st) != SUCCESS)
        { (void)db_rollback(); return ERROR_DATABASE; }
    int bound = db_bind_text(st, 1, hash) == SUCCESS && db_bind_int64(st, 2, (long long)teacher_user_id) == SUCCESS;
    int step = bound ? sqlite3_step(st) : SQLITE_ERROR;
    sqlite3_finalize(st);
    if (step != SQLITE_DONE || db_commit() != SUCCESS) { (void)db_rollback(); return ERROR_DATABASE; }
    memcpy(code_out, code, sizeof code);
    return SUCCESS;
#endif
}

int anonymous_teacher_link_claim(const char *sender_ref, const char *source_code)
{
    if (!sender_ref || !*sender_ref || strlen(sender_ref) >= 80 || !source_code) return ERROR_INVALID_INPUT;
    for (const unsigned char *p = (const unsigned char *)sender_ref; *p; ++p)
        if (!(isalnum(*p) || *p == '_' || *p == '-')) return ERROR_INVALID_INPUT;
    char code[17], hash[65];
    if (!normalize_code(source_code, code) || enrollment_hash(code, hash) != SUCCESS) return ERROR_NOT_FOUND;
    sqlite3_stmt *st = NULL;
    if (db_exec("BEGIN IMMEDIATE;") != SUCCESS) return ERROR_DATABASE;
    if (db_prepare("SELECT user_id FROM whatsapp_teacher_link_codes WHERE code_hash=? AND claimed_at IS NULL AND expires_at>datetime('now');", &st) != SUCCESS) {
        (void)db_rollback(); return ERROR_DATABASE;
    }
    db_bind_text(st, 1, hash);
    int step = sqlite3_step(st), found = step == SQLITE_ROW;
    int64_t user_id = found ? sqlite3_column_int64(st, 0) : 0;
    sqlite3_finalize(st);
    if (step != SQLITE_ROW && step != SQLITE_DONE) { (void)db_rollback(); return ERROR_DATABASE; }
    if (!found || user_id <= 0) { (void)db_rollback(); return ERROR_NOT_FOUND; }
    if (db_prepare("SELECT 1 FROM users u JOIN teachers t ON t.user_id=u.id WHERE u.id=? AND u.user_type='PROF' AND u.status='ACTIVE';", &st) != SUCCESS) {
        (void)db_rollback(); return ERROR_DATABASE;
    }
    db_bind_int64(st, 1, (long long)user_id);
    int active = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
    if (!active) { (void)db_rollback(); return ERROR_FORBIDDEN; }
    if (db_prepare("UPDATE whatsapp_teacher_link_codes SET claimed_by=?,claimed_at=datetime('now') WHERE code_hash=? AND claimed_at IS NULL AND expires_at>datetime('now');", &st) != SUCCESS) {
        (void)db_rollback(); return ERROR_DATABASE;
    }
    int bound = db_bind_text(st, 1, sender_ref) == SUCCESS && db_bind_text(st, 2, hash) == SUCCESS;
    step = bound ? sqlite3_step(st) : SQLITE_ERROR;
    int claimed = step == SQLITE_DONE && sqlite3_changes(database_get()) == 1;
    sqlite3_finalize(st);
    if (!claimed) { (void)db_rollback(); return step == SQLITE_DONE ? ERROR_NOT_FOUND : ERROR_DATABASE; }
    if (db_prepare("INSERT INTO whatsapp_teacher_links(from_ref,user_id) VALUES(?,?) ON CONFLICT(from_ref) DO NOTHING;", &st) != SUCCESS) {
        (void)db_rollback(); return ERROR_DATABASE;
    }
    bound = db_bind_text(st, 1, sender_ref) == SUCCESS && db_bind_int64(st, 2, (long long)user_id) == SUCCESS;
    step = bound ? sqlite3_step(st) : SQLITE_ERROR;
    sqlite3_finalize(st);
    if (step != SQLITE_DONE) { (void)db_rollback(); return ERROR_CONFLICT; }
    if (db_prepare("SELECT 1 FROM whatsapp_teacher_links WHERE from_ref=? AND user_id=?;", &st) != SUCCESS) {
        (void)db_rollback(); return ERROR_DATABASE;
    }
    bound = db_bind_text(st, 1, sender_ref) == SUCCESS && db_bind_int64(st, 2, (long long)user_id) == SUCCESS;
    step = bound ? sqlite3_step(st) : SQLITE_ERROR;
    sqlite3_finalize(st);
    if (step != SQLITE_ROW) { (void)db_rollback(); return step == SQLITE_DONE ? ERROR_CONFLICT : ERROR_DATABASE; }
    if (db_commit() != SUCCESS) { (void)db_rollback(); return ERROR_DATABASE; }
    return SUCCESS;
}

int anonymous_teacher_link_user(const char *sender_ref, int64_t *user_id_out)
{
    if (!sender_ref || !user_id_out) return ERROR_INVALID_INPUT;
    sqlite3_stmt *st = NULL;
    if (db_prepare("SELECT l.user_id FROM whatsapp_teacher_links l JOIN users u ON u.id=l.user_id JOIN teachers t ON t.user_id=u.id WHERE l.from_ref=? AND u.user_type='PROF' AND u.status='ACTIVE';", &st) != SUCCESS) return ERROR_DATABASE;
    db_bind_text(st, 1, sender_ref);
    int step = sqlite3_step(st);
    if (step == SQLITE_ROW) *user_id_out = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return step == SQLITE_ROW ? SUCCESS : step == SQLITE_DONE ? ERROR_NOT_FOUND : ERROR_DATABASE;
}

int anonymous_teacher_link_remove(int64_t teacher_user_id, int *was_linked_out)
{
    if (teacher_user_id <= 0 || !was_linked_out) return ERROR_INVALID_INPUT;
    *was_linked_out = 0;

    char sender_ref[80] = {0};
    sqlite3_stmt *st = NULL;
    if (db_exec("BEGIN IMMEDIATE;") != SUCCESS) return ERROR_DATABASE;
    if (db_prepare("SELECT from_ref FROM whatsapp_teacher_links WHERE user_id=?;", &st) != SUCCESS) {
        (void)db_rollback();
        return ERROR_DATABASE;
    }
    if (db_bind_int64(st, 1, (long long)teacher_user_id) != SUCCESS) {
        sqlite3_finalize(st);
        (void)db_rollback();
        return ERROR_DATABASE;
    }
    int step = sqlite3_step(st);
    if (step == SQLITE_ROW) {
        const unsigned char *value = sqlite3_column_text(st, 0);
        if (!value || strlen((const char *)value) >= sizeof sender_ref) {
            sqlite3_finalize(st);
            (void)db_rollback();
            return ERROR_DATABASE;
        }
        snprintf(sender_ref, sizeof sender_ref, "%s", (const char *)value);
    } else if (step != SQLITE_DONE) {
        sqlite3_finalize(st);
        (void)db_rollback();
        return ERROR_DATABASE;
    }
    sqlite3_finalize(st);

    if (sender_ref[0]) {
        static const char *cleanup_sql[] = {
            "DELETE FROM pending_tasks WHERE phone=?;",
            "DELETE FROM pending_teacher_task_edits WHERE from_ref=? AND user_id=?;",
            "DELETE FROM pending_teacher_notices WHERE from_ref=? AND user_id=?;",
            "DELETE FROM pending_teacher_notice_edits WHERE from_ref=? AND user_id=?;"
        };
        for (size_t i = 0; i < sizeof cleanup_sql / sizeof cleanup_sql[0]; ++i) {
            if (db_prepare(cleanup_sql[i], &st) != SUCCESS) {
                (void)db_rollback();
                return ERROR_DATABASE;
            }
            int bound = db_bind_text(st, 1, sender_ref) == SUCCESS;
            if (i != 0) bound = bound && db_bind_int64(st, 2, (long long)teacher_user_id) == SUCCESS;
            step = bound ? sqlite3_step(st) : SQLITE_ERROR;
            sqlite3_finalize(st);
            if (step != SQLITE_DONE) {
                (void)db_rollback();
                return ERROR_DATABASE;
            }
        }
        if (db_prepare("DELETE FROM whatsapp_teacher_links WHERE user_id=?;", &st) != SUCCESS) {
            (void)db_rollback();
            return ERROR_DATABASE;
        }
        if (db_bind_int64(st, 1, (long long)teacher_user_id) != SUCCESS) {
            sqlite3_finalize(st);
            (void)db_rollback();
            return ERROR_DATABASE;
        }
        step = sqlite3_step(st);
        int removed = step == SQLITE_DONE && sqlite3_changes(database_get()) == 1;
        sqlite3_finalize(st);
        if (step != SQLITE_DONE || !removed) {
            (void)db_rollback();
            return ERROR_DATABASE;
        }
        *was_linked_out = 1;
    }

    if (db_prepare("DELETE FROM whatsapp_teacher_link_codes WHERE user_id=?;", &st) != SUCCESS) {
        (void)db_rollback();
        return ERROR_DATABASE;
    }
    if (db_bind_int64(st, 1, (long long)teacher_user_id) != SUCCESS) {
        sqlite3_finalize(st);
        (void)db_rollback();
        return ERROR_DATABASE;
    }
    step = sqlite3_step(st);
    sqlite3_finalize(st);
    if (step != SQLITE_DONE || db_commit() != SUCCESS) {
        (void)db_rollback();
        *was_linked_out = 0;
        return ERROR_DATABASE;
    }
    return SUCCESS;
}

static int normalize_code(const char *source, char out[17])
{
    static const char alphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    if (!source) return 0;
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)source; *p; ++p) {
        if (*p == ' ' || *p == '-' || *p == '\t') continue;
        unsigned char c = (unsigned char)toupper(*p);
        if (!strchr(alphabet, c) || used >= 16) return 0;
        out[used++] = (char)c;
    }
    if (used != 16) return 0;
    out[used] = '\0';
    return 1;
}

int anonymous_enrollment_create(int64_t coordinator_id, int64_t class_id,
                                char code_out[17])
{
    if (coordinator_id <= 0 || class_id <= 0 || !code_out) return ERROR_INVALID_INPUT;
#if !defined(HAVE_OPENSSL_SHA)
    return ERROR_CONFIG;
#else
    Class classroom;
    if (classes_find_by_id(class_id, &classroom) != SUCCESS ||
        strcmp(classroom.status, "ACTIVE") != 0) return ERROR_NOT_FOUND;

    static const char alphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    unsigned char random[16];
    if (RAND_bytes(random, (int)sizeof random) != 1) return ERROR_INTERNAL;
    char code[17], hash[65];
    for (size_t i = 0; i < 16; ++i) code[i] = alphabet[random[i] & 31U];
    code[16] = '\0';
    if (enrollment_hash(code, hash) != SUCCESS) return ERROR_INTERNAL;

    (void)db_exec("DELETE FROM anonymous_enrollment_codes WHERE expires_at<=datetime('now') "
                  "OR claimed_at<=datetime('now','-30 days');");
    sqlite3_stmt *statement = NULL;
    if (db_prepare("INSERT INTO anonymous_enrollment_codes(code_hash,class_id,created_by,expires_at) "
                   "VALUES(?,?,?,datetime('now','+1 day'));", &statement) != SUCCESS)
        return ERROR_DATABASE;
    int bind_ok = db_bind_text(statement, 1, hash) == SUCCESS &&
                  db_bind_int64(statement, 2, (long long)class_id) == SUCCESS &&
                  db_bind_int64(statement, 3, (long long)coordinator_id) == SUCCESS;
    int step = bind_ok ? sqlite3_step(statement) : SQLITE_ERROR;
    sqlite3_finalize(statement);
    if (step != SQLITE_DONE) return ERROR_DATABASE;
    memcpy(code_out, code, sizeof code);
    return SUCCESS;
#endif
}

int anonymous_enrollment_claim(const char *sender_ref, const char *source_code,
                               int64_t *class_id_out)
{
    if (!sender_ref || !*sender_ref || strlen(sender_ref) >= 80 || !class_id_out)
        return ERROR_INVALID_INPUT;
    char code[17], hash[65];
    if (!normalize_code(source_code, code) || enrollment_hash(code, hash) != SUCCESS)
        return ERROR_NOT_FOUND;

    sqlite3_stmt *statement = NULL;
    /* Serialize claims so the same one-time code cannot be consumed twice. */
    if (db_exec("BEGIN IMMEDIATE;") != SUCCESS) return ERROR_DATABASE;
    if (db_prepare("SELECT class_id FROM anonymous_enrollment_codes "
                   "WHERE code_hash=? AND claimed_at IS NULL AND expires_at>datetime('now');",
                   &statement) != SUCCESS) {
        (void)db_rollback();
        return ERROR_DATABASE;
    }
    int bound = db_bind_text(statement, 1, hash) == SUCCESS;
    int select_step = bound ? sqlite3_step(statement) : SQLITE_ERROR;
    int found = select_step == SQLITE_ROW;
    int64_t class_id = found ? sqlite3_column_int64(statement, 0) : 0;
    sqlite3_finalize(statement);
    if (select_step != SQLITE_ROW && select_step != SQLITE_DONE) {
        (void)db_rollback(); return ERROR_DATABASE;
    }
    if (!found || class_id <= 0) { (void)db_rollback(); return ERROR_NOT_FOUND; }

    if (db_prepare("SELECT 1 FROM classes WHERE id=? AND status='ACTIVE';", &statement) != SUCCESS) {
        (void)db_rollback();
        return ERROR_DATABASE;
    }
    if (db_bind_int64(statement, 1, (long long)class_id) != SUCCESS) {
        sqlite3_finalize(statement);
        (void)db_rollback();
        return ERROR_DATABASE;
    }
    int class_active = sqlite3_step(statement) == SQLITE_ROW;
    sqlite3_finalize(statement);
    if (!class_active) { (void)db_rollback(); return ERROR_NOT_FOUND; }

    if (db_prepare("UPDATE anonymous_enrollment_codes SET claimed_by=?,claimed_at=datetime('now') "
                   "WHERE code_hash=? AND claimed_at IS NULL AND expires_at>datetime('now');",
                   &statement) != SUCCESS) {
        (void)db_rollback();
        return ERROR_DATABASE;
    }
    bound = db_bind_text(statement, 1, sender_ref) == SUCCESS &&
                db_bind_text(statement, 2, hash) == SUCCESS;
    int step = bound ? sqlite3_step(statement) : SQLITE_ERROR;
    int claimed = step == SQLITE_DONE && sqlite3_changes(database_get()) == 1;
    sqlite3_finalize(statement);
    if (!claimed) { (void)db_rollback(); return step == SQLITE_DONE ? ERROR_NOT_FOUND : ERROR_DATABASE; }

    if (db_prepare("INSERT INTO anonymous_students(phone,class_id,class_verified,pending_question) "
                   "VALUES(?,?,1,NULL) ON CONFLICT(phone) DO UPDATE SET "
                   "class_id=excluded.class_id,class_verified=1,pending_question=NULL,updated_at=datetime('now');",
                   &statement) != SUCCESS) {
        (void)db_rollback();
        return ERROR_DATABASE;
    }
    bound = db_bind_text(statement, 1, sender_ref) == SUCCESS &&
            db_bind_int64(statement, 2, (long long)class_id) == SUCCESS;
    step = bound ? sqlite3_step(statement) : SQLITE_ERROR;
    sqlite3_finalize(statement);
    if (step != SQLITE_DONE || db_commit() != SUCCESS) {
        (void)db_rollback();
        return ERROR_DATABASE;
    }
    *class_id_out = class_id;
    return SUCCESS;
}
