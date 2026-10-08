/* tasks.h
 * CRUD de tarefas com validação obrigatória:
 *   - O professor deve estar vinculado à turma.
 *   - Apenas o dono (ou coordenador) pode editar/excluir.
 *   - Datas em ISO 8601.
 */
#ifndef SCHOOL_TASKS_H
#define SCHOOL_TASKS_H

#include "error.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum {
    TASK_STATUS_ACTIVE   = 0,
    TASK_STATUS_ARCHIVED = 1
} TaskStatus;

typedef struct {
    int64_t    id;
    int64_t    teacher_id;
    int64_t    class_id;
    char       subject[64];
    char       title[121];
    char       description[1024];
    char       due_date[11];        /* YYYY-MM-DD */
    TaskStatus status;
    char       created_at[32];
    char       updated_at[32];
} Task;

const char *task_status_str(TaskStatus s);
TaskStatus  task_status_from(const char *s);

int tasks_create(int64_t teacher_id, int64_t class_id,
                 const char *subject, const char *title,
                 const char *description, const char *due_date,
                 Task *out);

int tasks_update(int64_t task_id, int64_t acting_teacher_id, bool is_coord,
                 const char *subject, const char *title,
                 const char *description, const char *due_date);

int tasks_delete(int64_t task_id, int64_t acting_teacher_id, bool is_coord);
int tasks_find_by_id(int64_t id, Task *out);

/* Lista tarefas por turma (aluno/consulta). */
int tasks_list_by_class(int64_t class_id, bool include_archived,
                        Task **out, size_t *count);

/* Lista tarefas de um professor. */
int tasks_list_by_teacher(int64_t teacher_id, bool include_archived,
                          Task **out, size_t *count);

/* Lista o histórico global (uso exclusivo da coordenação via API). */
int tasks_list_all(bool include_archived, Task **out, size_t *count);

#endif /* SCHOOL_TASKS_H */
