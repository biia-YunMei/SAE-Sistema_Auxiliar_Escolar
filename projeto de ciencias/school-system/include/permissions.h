/* permissions.h
 * RBAC simples baseado em UserType.
 * Toda verificação passa pelo back-end — nunca confie no cliente.
 */
#ifndef SCHOOL_PERMISSIONS_H
#define SCHOOL_PERMISSIONS_H

#include "error.h"
#include "users.h"

typedef enum {
    PERM_NONE = 0,
    PERM_USER_CREATE,
    PERM_USER_LIST,
    PERM_CLASS_CREATE,
    PERM_TEACHER_LINK_CLASS,
    PERM_TASK_CREATE,
    PERM_TASK_EDIT_OWN,
    PERM_TASK_EDIT_ANY,
    PERM_TASK_DELETE_OWN,
    PERM_TASK_DELETE_ANY,
    PERM_TASK_READ,
    PERM_EXAM_CREATE,
    PERM_ASSIGNMENT_CREATE,
    PERM_NOTICE_CREATE,
    PERM_AI_USE
} Permission;

bool permissions_check(UserType type, Permission p);

#endif /* SCHOOL_PERMISSIONS_H */
