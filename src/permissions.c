/* permissions.c
 * Matriz de permissões por tipo de usuário.
 */
#include "permissions.h"
#include <string.h>

bool permissions_check(UserType type, Permission p)
{
    switch (p) {
    case PERM_USER_CREATE:
    case PERM_USER_LIST:
    case PERM_CLASS_CREATE:
    case PERM_TEACHER_LINK_CLASS:
    case PERM_TASK_EDIT_ANY:
    case PERM_TASK_DELETE_ANY:
    case PERM_NOTICE_CREATE:
    case PERM_AI_USE:
        return type == USER_TYPE_COORD;

    case PERM_TASK_CREATE:
    case PERM_TASK_EDIT_OWN:
    case PERM_TASK_DELETE_OWN:
    case PERM_TASK_READ:
    case PERM_EXAM_CREATE:
    case PERM_ASSIGNMENT_CREATE:
        return type == USER_TYPE_COORD || type == USER_TYPE_PROF;

    case PERM_NONE:
    default:
        return false;
    }
}
