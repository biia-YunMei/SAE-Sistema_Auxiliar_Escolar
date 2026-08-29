/* tasks.c
 * Implementação do CRUD de tarefas.
 *
 * Regras:
 *   - Professor precisa estar vinculado à turma (teacher_classes).
 *   - Edição/exclusão: somente o dono, exceto coordenador.
 *   - Datas validadas em ISO 8601.
 *   - Strings longas: TRUNCADAS com validação prévia.
 */
#include "tasks.h"
#include "classes.h"
#include "database.h"
#include "validation.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *task_status_str(TaskStatus s)
{
    switch (s) {
    case TASK_STATUS_ACTIVE:   return "ACTIVE";
    case TASK_STATUS_ARCHIVED: return "ARCHIVED";
    }
    return "ACTIVE";
}

TaskStatus task_status_from(const char *s)
{
    if (!s) return TASK_STATUS_ACTIVE;
    if (strcmp(s, "ARCHIVED") == 0) return TASK_STATUS_ARCHIVED;
    return TASK_STATUS_ACTIVE;
}

/* Validação comum dos campos de texto. */
static int validate_fields(const char *subject, const char *title,
                           const char *due_date)
{
    if (!subject || !title || !due_date) return ERROR_INVALID_INPUT;
    if (!validation_length_between(subject, 1, 63))  return ERROR_TOO_LONG;
    if (!validation_length_between(title,   1, 120)) return ERROR_TOO_LONG;
    if (!validation_is_valid_date(due_date))        return ERROR_INVALID_DATE;
    return SUCCESS;
}

static int fetch_task(sqlite3_stmt *st, Task *out)
{
    out->id          = sqlite3_column_int64(st, 0);
    out->teacher_id  = sqlite3_column_int64(st, 1);
    out->class_id    = sqlite3_column_int64(st, 2);
    snprintf(out->subject,   sizeof out->subject,   "%s",
             (const char *)sqlite3_column_text(st, 3));
    snprintf(out->title,     sizeof out->title,     "%s",
             (const char *)sqlite3_column_text(st, 4));
    const unsigned char *d = sqlite3_column_text(st, 5);
    snprintf(out->description, sizeof out->description, "%s",
             d ? (const char *)d : "");
    snprintf(out->due_date, sizeof out->due_date, "%s",
             (const char *)sqlite3_column_text(st, 6));
    const unsigned char *s = sqlite3_column_text(st, 7);
    out->status = task_status_from(s ? (const char *)s : "ACTIVE");
    snprintf(out->created_at, sizeof out->created_at, "%s",
             (const char *)sqlite3_column_text(st, 8));
    snprintf(out->updated_at, sizeof out->updated_at, "%s",
             (const char *)sqlite3_column_text(st, 9));
    return SUCCESS;
}

int tasks_create(int64_t teacher_id, int64_t class_id,
                 const char *subject, const char *title,
                 const char *description, const char *due_date,
                 Task *out)
{
    int rc = validate_fields(subject, title, due_date);
    if (rc != SUCCESS) return rc;
    if (!classes_teacher_has_access(teacher_id, class_id))
        return ERROR_FORBIDDEN;

    const char *desc = description ? description : "";

    sqlite3_stmt *st = NULL;
    rc = db_prepare(
        "INSERT INTO tasks(teacher_id, class_id, subject, title, description, due_date) "
        "VALUES(?,?,?,?,?,?);", &st);
    if (rc != SUCCESS) return rc;
    db_bind_int64(st, 1, (long long)teacher_id);
    db_bind_int64(st, 2, (long long)class_id);
    db_bind_text (st, 3, subject);
    db_bind_text (st, 4, title);
    db_bind_text (st, 5, desc);
    db_bind_text (st, 6, due_date);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return ERROR_DATABASE;

    int64_t task_id = (int64_t)sqlite3_last_insert_rowid(database_get());
    log_info("Tarefa criada id=%lld teacher=%lld class=%lld",
             (long long)task_id, (long long)teacher_id, (long long)class_id);

    return tasks_find_by_id(task_id, out);
}

int tasks_find_by_id(int64_t id, Task *out)
{
    if (!out) return ERROR_INVALID_INPUT;
    sqlite3_stmt *st = NULL;
    int rc = db_prepare(
        "SELECT id, teacher_id, class_id, subject, title, description, due_date, "
        "       status, created_at, updated_at "
        "FROM tasks WHERE id=? LIMIT 1;", &st);
    if (rc != SUCCESS) return rc;
    db_bind_int64(st, 1, (long long)id);
    if (sqlite3_step(st) == SQLITE_ROW) {
        fetch_task(st, out);
        sqlite3_finalize(st);
        return SUCCESS;
    }
    sqlite3_finalize(st);
    return ERROR_NOT_FOUND;
}

int tasks_update(int64_t task_id, int64_t acting_teacher_id, bool is_coord,
                 const char *subject, const char *title,
                 const char *description, const char *due_date)
{
    int rc = validate_fields(subject, title, due_date);
    if (rc != SUCCESS) return rc;

    Task current;
    rc = tasks_find_by_id(task_id, &current);
    if (rc != SUCCESS) return rc;

    if (!is_coord && current.teacher_id != acting_teacher_id) {
        log_warn("Tentativa de edição negada: teacher=%lld != owner=%lld",
                 (long long)acting_teacher_id, (long long)current.teacher_id);
        return ERROR_FORBIDDEN;
    }

    sqlite3_stmt *st = NULL;
    rc = db_prepare(
        "UPDATE tasks SET subject=?, title=?, description=?, due_date=?, "
        "updated_at=datetime('now') WHERE id=?;", &st);
    if (rc != SUCCESS) return rc;
    db_bind_text (st, 1, subject);
    db_bind_text (st, 2, title);
    db_bind_text (st, 3, description ? description : "");
    db_bind_text (st, 4, due_date);
    db_bind_int64(st, 5, (long long)task_id);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return ERROR_DATABASE;

    log_info("Tarefa atualizada id=%lld por teacher=%lld (coord=%d)",
             (long long)task_id, (long long)acting_teacher_id, is_coord ? 1 : 0);
    return SUCCESS;
}

int tasks_delete(int64_t task_id, int64_t acting_teacher_id, bool is_coord)
{
    Task current;
    int rc = tasks_find_by_id(task_id, &current);
    if (rc != SUCCESS) return rc;

    if (!is_coord && current.teacher_id != acting_teacher_id)
        return ERROR_FORBIDDEN;

    sqlite3_stmt *st = NULL;
    rc = db_prepare("DELETE FROM tasks WHERE id=?;", &st);
    if (rc != SUCCESS) return rc;
    db_bind_int64(st, 1, (long long)task_id);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return ERROR_DATABASE;
    log_info("Tarefa excluída id=%lld por teacher=%lld (coord=%d)",
             (long long)task_id, (long long)acting_teacher_id, is_coord ? 1 : 0);
    return SUCCESS;
}

static int list_by(const char *sql, int64_t id, bool include_archived,
                   Task **out, size_t *count)
{
    if (!out || !count) return ERROR_INVALID_INPUT;
    *out = NULL; *count = 0;

    char final_sql[512];
    if (include_archived)
        snprintf(final_sql, sizeof final_sql, "%s", sql);
    else
        snprintf(final_sql, sizeof final_sql,
                 "SELECT id,teacher_id,class_id,subject,title,description,due_date,"
                 "status,created_at,updated_at FROM (%s) "
                 "WHERE status='ACTIVE';", sql);

    sqlite3_stmt *st = NULL;
    int rc = db_prepare(final_sql, &st);
    if (rc != SUCCESS) return rc;
    db_bind_int64(st, 1, (long long)id);

    size_t cap = 8, n = 0;
    Task *arr = (Task *)malloc(cap * sizeof(Task));
    if (!arr) { sqlite3_finalize(st); return ERROR_INTERNAL; }
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (n == cap) {
            cap *= 2;
            Task *p = (Task *)realloc(arr, cap * sizeof(Task));
            if (!p) { free(arr); sqlite3_finalize(st); return ERROR_INTERNAL; }
            arr = p;
        }
        fetch_task(st, &arr[n++]);
    }
    sqlite3_finalize(st);
    *out = arr; *count = n;
    return SUCCESS;
}

int tasks_list_by_class(int64_t class_id, bool include_archived,
                        Task **out, size_t *count)
{
    return list_by("SELECT id,teacher_id,class_id,subject,title,description,due_date,"
                   "status,created_at,updated_at FROM tasks WHERE class_id=?",
                   class_id, include_archived, out, count);
}

int tasks_list_by_teacher(int64_t teacher_id, bool include_archived,
                          Task **out, size_t *count)
{
    return list_by("SELECT id,teacher_id,class_id,subject,title,description,due_date,"
                   "status,created_at,updated_at FROM tasks WHERE teacher_id=?",
                   teacher_id, include_archived, out, count);
}
