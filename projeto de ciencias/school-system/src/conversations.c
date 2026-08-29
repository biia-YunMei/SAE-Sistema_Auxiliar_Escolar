/* conversations.c
 * Implementação da máquina de estados — persiste no SQLite.
 */
#include "conversations.h"
#include "database.h"
#include "validation.h"
#include "logger.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

const char *conv_state_str(ConversationState s)
{
    switch (s) {
    case CONV_IDLE:                  return "IDLE";
    case CONV_RECEIVING:             return "RECEIVING";
    case CONV_PROCESSING:            return "PROCESSING";
    case CONV_WAITING_AI:            return "WAITING_AI";
    case CONV_WAITING_TRANSCRIPTION: return "WAITING_TRANSCRIPTION";
    case CONV_AWAITING_CONFIRMATION: return "AWAITING_CONFIRMATION";
    case CONV_CONFIRMED:             return "CONFIRMED";
    case CONV_SAVED:                 return "SAVED";
    case CONV_CANCELLED:             return "CANCELLED";
    case CONV_ERROR:                 return "ERROR";
    }
    return "IDLE";
}

static ConversationState state_from(const char *s)
{
    if (!s) return CONV_IDLE;
    if (!strcmp(s, "RECEIVING"))             return CONV_RECEIVING;
    if (!strcmp(s, "PROCESSING"))            return CONV_PROCESSING;
    if (!strcmp(s, "WAITING_AI"))            return CONV_WAITING_AI;
    if (!strcmp(s, "WAITING_TRANSCRIPTION")) return CONV_WAITING_TRANSCRIPTION;
    if (!strcmp(s, "AWAITING_CONFIRMATION")) return CONV_AWAITING_CONFIRMATION;
    if (!strcmp(s, "CONFIRMED"))             return CONV_CONFIRMED;
    if (!strcmp(s, "SAVED"))                 return CONV_SAVED;
    if (!strcmp(s, "CANCELLED"))             return CONV_CANCELLED;
    if (!strcmp(s, "ERROR"))                 return CONV_ERROR;
    return CONV_IDLE;
}

int conv_get_or_create(const char *phone, int64_t *out_id)
{
    if (!phone || !validation_is_valid_phone(phone)) return ERROR_INVALID_PHONE;
    sqlite3_stmt *st = NULL;
    int rc = db_prepare(
        "SELECT id FROM conversations WHERE phone=? LIMIT 1;", &st);
    if (rc != SUCCESS) return rc;
    db_bind_text(st, 1, phone);
    if (sqlite3_step(st) == SQLITE_ROW) {
        int64_t id = sqlite3_column_int64(st, 0);
        sqlite3_finalize(st);
        if (out_id) *out_id = id;
        return SUCCESS;
    }
    sqlite3_finalize(st);

    rc = db_prepare(
        "INSERT INTO conversations(phone, state) VALUES(?, 'IDLE');", &st);
    if (rc != SUCCESS) return rc;
    db_bind_text(st, 1, phone);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return ERROR_DATABASE;
    if (out_id) *out_id = (int64_t)sqlite3_last_insert_rowid(database_get());
    return SUCCESS;
}

int conv_set_state(int64_t id, ConversationState s)
{
    sqlite3_stmt *st = NULL;
    int rc = db_prepare(
        "UPDATE conversations SET state=?, updated_at=datetime('now') WHERE id=?;", &st);
    if (rc != SUCCESS) return rc;
    db_bind_text(st, 1, conv_state_str(s));
    db_bind_int64(st, 2, (long long)id);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? SUCCESS : ERROR_DATABASE;
}

int conv_get_state(int64_t id, ConversationState *out)
{
    if (!out) return ERROR_INVALID_INPUT;
    sqlite3_stmt *st = NULL;
    int rc = db_prepare("SELECT state FROM conversations WHERE id=?;", &st);
    if (rc != SUCCESS) return rc;
    db_bind_int64(st, 1, (long long)id);
    rc = sqlite3_step(st);
    if (rc != SQLITE_ROW) { sqlite3_finalize(st); return ERROR_NOT_FOUND; }
    const unsigned char *s = sqlite3_column_text(st, 0);
    *out = state_from(s ? (const char *)s : "IDLE");
    sqlite3_finalize(st);
    return SUCCESS;
}

int conv_set_context(int64_t id, const char *ctx)
{
    sqlite3_stmt *st = NULL;
    int rc = db_prepare(
        "UPDATE conversations SET context_json=?, updated_at=datetime('now') "
        "WHERE id=?;", &st);
    if (rc != SUCCESS) return rc;
    if (ctx) db_bind_text(st, 1, ctx);
    else     db_bind_null(st, 1);
    db_bind_int64(st, 2, (long long)id);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? SUCCESS : ERROR_DATABASE;
}
