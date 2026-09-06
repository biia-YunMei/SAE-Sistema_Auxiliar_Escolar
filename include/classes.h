/* classes.h
 * Turmas e vínculos professor↔turma.
 */
#ifndef SCHOOL_CLASSES_H
#define SCHOOL_CLASSES_H

#include "error.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    SHIFT_MORNING   = 0,
    SHIFT_AFTERNOON = 1,
    SHIFT_EVENING   = 2,
    SHIFT_FULL      = 3
} ClassShift;

typedef struct {
    int64_t id;
    char    name[64];
    char    grade[32];
    ClassShift shift;
    char    status[16];
} Class;

const char *class_shift_str(ClassShift s);
ClassShift  class_shift_from(const char *s);

int classes_create(const char *name, const char *grade, ClassShift shift, Class *out);
int classes_update(int64_t id, const char *name, const char *grade, ClassShift shift, Class *out);
int classes_delete(int64_t id);
int classes_list(Class **out, size_t *count);
int classes_find_by_id(int64_t id, Class *out);
int classes_find_by_name(const char *name, Class *out);

/* Vincula professor a uma turma (N:N). */
int classes_link_teacher(int64_t teacher_id, int64_t class_id);
int classes_unlink_teacher(int64_t teacher_id, int64_t class_id);

/* Verifica se o professor tem acesso à turma. */
int classes_teacher_has_access(int64_t teacher_id, int64_t class_id);

/* Lista turmas de um professor. */
int classes_list_for_teacher(int64_t teacher_id, int64_t **out_ids, size_t *count);

#endif /* SCHOOL_CLASSES_H */
