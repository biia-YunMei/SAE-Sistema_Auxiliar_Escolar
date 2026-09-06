/* classes.c
 * Turmas + vínculos professor↔turma.
 */
#include "classes.h"
#include "database.h"
#include "validation.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *class_shift_str(ClassShift s)
{
    switch (s) {
    case SHIFT_MORNING:   return "MORNING";
    case SHIFT_AFTERNOON: return "AFTERNOON";
    case SHIFT_EVENING:   return "EVENING";
    case SHIFT_FULL:      return "FULL";
    }
    return "MORNING";
}

ClassShift class_shift_from(const char *s)
{
    if (!s) return SHIFT_MORNING;
    if (strcmp(s, "MORNING")   == 0) return SHIFT_MORNING;
    if (strcmp(s, "AFTERNOON") == 0) return SHIFT_AFTERNOON;
    if (strcmp(s, "EVENING")   == 0) return SHIFT_EVENING;
    if (strcmp(s, "FULL")      == 0) return SHIFT_FULL;
    return SHIFT_MORNING;
}

static int fetch_class_by(const char *sql, const char *param, Class *out)
{
    sqlite3_stmt *st = NULL;
    int rc = db_prepare(sql, &st);
    if (rc != SUCCESS) return rc;
    if (param) db_bind_text(st, 1, param);
    rc = sqlite3_step(st);
    if (rc == SQLITE_ROW && out) {
        out->id   = sqlite3_column_int64(st, 0);
        const unsigned char *n = sqlite3_column_text(st, 1);
        const unsigned char *g = sqlite3_column_text(st, 2);
        const unsigned char *sh= sqlite3_column_text(st, 3);
        const unsigned char *stt=sqlite3_column_text(st, 4);
        snprintf(out->name,  sizeof out->name,  "%s", n  ? (const char*)n  : "");
        snprintf(out->grade, sizeof out->grade, "%s", g  ? (const char*)g  : "");
        out->shift = class_shift_from(sh ? (const char*)sh : "");
        snprintf(out->status, sizeof out->status, "%s", stt? (const char*)stt: "");
    } else if (rc == SQLITE_ROW) {
        rc = SUCCESS;
    } else {
        rc = ERROR_NOT_FOUND;
    }
    sqlite3_finalize(st);
    return rc == SQLITE_ROW ? SUCCESS : rc;
}

int classes_create(const char *name, const char *grade, ClassShift shift, Class *out)
{
    if (!name || !grade) return ERROR_INVALID_INPUT;
    if (!validation_length_between(name, 1, 63)) return ERROR_TOO_LONG;
    if (!validation_length_between(grade, 1, 31)) return ERROR_TOO_LONG;

    sqlite3_stmt *st = NULL;
    int rc = db_prepare(
        "INSERT INTO classes(name, grade, shift) VALUES(?,?,?);", &st);
    if (rc != SUCCESS) return rc;
    db_bind_text(st, 1, name);
    db_bind_text(st, 2, grade);
    db_bind_text(st, 3, class_shift_str(shift));
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) {
        return rc == SQLITE_CONSTRAINT ? ERROR_ALREADY_EXISTS : ERROR_DATABASE;
    }
    return classes_find_by_name(name, out);
}

int classes_update(int64_t id, const char *name, const char *grade, ClassShift shift, Class *out)
{
    if (id <= 0 || !name || !grade) return ERROR_INVALID_INPUT;
    if (!validation_length_between(name, 1, 63)) return ERROR_TOO_LONG;
    if (!validation_length_between(grade, 1, 31)) return ERROR_TOO_LONG;

    sqlite3_stmt *st = NULL;
    int rc = db_prepare("UPDATE classes SET name=?, grade=?, shift=? WHERE id=?;", &st);
    if (rc != SUCCESS) return rc;
    db_bind_text(st, 1, name);
    db_bind_text(st, 2, grade);
    db_bind_text(st, 3, class_shift_str(shift));
    db_bind_int64(st, 4, (long long)id);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc == SQLITE_CONSTRAINT) return ERROR_ALREADY_EXISTS;
    if (rc != SQLITE_DONE) return ERROR_DATABASE;
    if (sqlite3_changes(database_get()) == 0) return ERROR_NOT_FOUND;
    return classes_find_by_id(id, out);
}

int classes_delete(int64_t id)
{
    if (id <= 0) return ERROR_INVALID_INPUT;
    sqlite3_stmt *st = NULL;
    int rc = db_prepare("DELETE FROM classes WHERE id=?;", &st);
    if (rc != SUCCESS) return rc;
    db_bind_int64(st, 1, (long long)id);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return ERROR_DATABASE;
    return sqlite3_changes(database_get()) ? SUCCESS : ERROR_NOT_FOUND;
}

/* helper extra */
int classes_find_by_name(const char *name, Class *out)
{
    return fetch_class_by(
        "SELECT id,name,grade,shift,status FROM classes WHERE name=? LIMIT 1;",
        name, out);
}

int classes_find_by_id(int64_t id, Class *out)
{
    char buf[32]; snprintf(buf, sizeof buf, "%lld", (long long)id);
    return fetch_class_by(
        "SELECT id,name,grade,shift,status FROM classes WHERE id=? LIMIT 1;",
        buf, out);
}

int classes_list(Class **out, size_t *count)
{
    if (!out || !count) return ERROR_INVALID_INPUT;
    *out = NULL; *count = 0;
    sqlite3_stmt *st = NULL;
    int rc = db_prepare("SELECT id,name,grade,shift,status FROM classes;", &st);
    if (rc != SUCCESS) return rc;
    size_t cap = 8, n = 0;
    Class *arr = (Class *)malloc(cap * sizeof(Class));
    if (!arr) { sqlite3_finalize(st); return ERROR_INTERNAL; }
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (n == cap) {
            cap *= 2;
            Class *p = (Class *)realloc(arr, cap * sizeof(Class));
            if (!p) { free(arr); sqlite3_finalize(st); return ERROR_INTERNAL; }
            arr = p;
        }
        arr[n].id = sqlite3_column_int64(st, 0);
        snprintf(arr[n].name,  sizeof arr[n].name,  "%s",
                 (const char*)sqlite3_column_text(st, 1));
        snprintf(arr[n].grade, sizeof arr[n].grade, "%s",
                 (const char*)sqlite3_column_text(st, 2));
        const unsigned char *sh = sqlite3_column_text(st, 3);
        const unsigned char *stt= sqlite3_column_text(st, 4);
        arr[n].shift = class_shift_from(sh ? (const char*)sh : "");
        snprintf(arr[n].status, sizeof arr[n].status, "%s",
                 stt ? (const char*)stt : "");
        n++;
    }
    sqlite3_finalize(st);
    *out = arr; *count = n;
    return SUCCESS;
}

int classes_link_teacher(int64_t teacher_id, int64_t class_id)
{
    sqlite3_stmt *st = NULL;
    int rc = db_prepare(
        "INSERT OR IGNORE INTO teacher_classes(teacher_id, class_id) VALUES(?,?);", &st);
    if (rc != SUCCESS) return rc;
    db_bind_int64(st, 1, (long long)teacher_id);
    db_bind_int64(st, 2, (long long)class_id);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? SUCCESS : ERROR_DATABASE;
}

int classes_unlink_teacher(int64_t teacher_id, int64_t class_id)
{
    sqlite3_stmt *st = NULL;
    int rc = db_prepare(
        "DELETE FROM teacher_classes WHERE teacher_id=? AND class_id=?;", &st);
    if (rc != SUCCESS) return rc;
    db_bind_int64(st, 1, (long long)teacher_id);
    db_bind_int64(st, 2, (long long)class_id);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? SUCCESS : ERROR_DATABASE;
}

int classes_teacher_has_access(int64_t teacher_id, int64_t class_id)
{
    sqlite3_stmt *st = NULL;
    int rc = db_prepare(
        "SELECT 1 FROM teacher_classes WHERE teacher_id=? AND class_id=? LIMIT 1;", &st);
    if (rc != SUCCESS) return rc;
    db_bind_int64(st, 1, (long long)teacher_id);
    db_bind_int64(st, 2, (long long)class_id);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_ROW ? 1 : 0;
}

int classes_list_for_teacher(int64_t teacher_id, int64_t **out_ids, size_t *count)
{
    if (!out_ids || !count) return ERROR_INVALID_INPUT;
    *out_ids = NULL; *count = 0;
    sqlite3_stmt *st = NULL;
    int rc = db_prepare(
        "SELECT class_id FROM teacher_classes WHERE teacher_id=?;", &st);
    if (rc != SUCCESS) return rc;
    db_bind_int64(st, 1, (long long)teacher_id);
    size_t cap = 8, n = 0;
    int64_t *arr = (int64_t *)malloc(cap * sizeof(int64_t));
    if (!arr) { sqlite3_finalize(st); return ERROR_INTERNAL; }
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (n == cap) {
            cap *= 2;
            int64_t *p = (int64_t *)realloc(arr, cap * sizeof(int64_t));
            if (!p) { free(arr); sqlite3_finalize(st); return ERROR_INTERNAL; }
            arr = p;
        }
        arr[n++] = sqlite3_column_int64(st, 0);
    }
    sqlite3_finalize(st);
    *out_ids = arr; *count = n;
    return SUCCESS;
}
