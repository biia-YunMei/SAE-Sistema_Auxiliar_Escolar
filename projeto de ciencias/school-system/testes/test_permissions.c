/* test_permissions.c
 * Testes da matriz de permissões e de turmas.
 */
#include "permissions.h"
#include "users.h"
#include "classes.h"
#include "database.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int passed = 0, failed = 0;
#define CHECK(expr) do { \
    if (expr) { passed++; } \
    else { failed++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #expr); } \
} while (0)

int main(void)
{
    logger_init("logs", LOG_ERROR);

    const char *tmp = "data/test_perm.db";
    unlink(tmp); unlink("data/test_perm.db-wal"); unlink("data/test_perm.db-shm");
    CHECK(database_init(tmp) == SUCCESS);

    /* Permissões: COORD tudo; PROF apenas "own"; STUD nada. */
    CHECK(permissions_check(USER_TYPE_COORD, PERM_USER_CREATE));
    CHECK(permissions_check(USER_TYPE_COORD, PERM_TASK_CREATE));
    CHECK(permissions_check(USER_TYPE_PROF,  PERM_TASK_CREATE));
    CHECK(!permissions_check(USER_TYPE_STUD, PERM_TASK_CREATE));
    CHECK(!permissions_check(USER_TYPE_STUD, PERM_USER_CREATE));
    CHECK(!permissions_check(USER_TYPE_STUD, PERM_NOTICE_CREATE));
    CHECK(!permissions_check(USER_TYPE_PROF, PERM_USER_CREATE));
    CHECK(!permissions_check(USER_TYPE_STUD, PERM_TASK_EDIT_ANY));

    /* Turmas */
    Class c;
    CHECK(classes_create("2A", "2o ano", SHIFT_MORNING, &c) == SUCCESS);
    CHECK(c.id > 0);
    CHECK(strcmp(c.name, "2A") == 0);

    /* Cadastra um professor */
    User prof;
    CHECK(users_create("Prof", "+5511988887777", USER_TYPE_PROF, NULL, &prof)
          == SUCCESS);
    CHECK(db_exec("INSERT INTO teachers(user_id) VALUES(2);") == SUCCESS);
    /* Após o INSERT, teacher_id = (id) do row; simplificamos usando SELECT. */
    int64_t teacher_id = 0;
    {
        sqlite3_stmt *st = NULL;
        db_prepare("SELECT id FROM teachers WHERE user_id=?;", &st);
        db_bind_int64(st, 1, (long long)prof.id);
        sqlite3_step(st);
        teacher_id = sqlite3_column_int64(st, 0);
        sqlite3_finalize(st);
    }
    CHECK(teacher_id > 0);

    CHECK(classes_teacher_has_access(teacher_id, c.id) == 0);
    CHECK(classes_link_teacher(teacher_id, c.id) == SUCCESS);
    CHECK(classes_teacher_has_access(teacher_id, c.id) == 1);

    int64_t *ids = NULL; size_t n = 0;
    CHECK(classes_list_for_teacher(teacher_id, &ids, &n) == SUCCESS);
    CHECK(n == 1 && ids[0] == c.id);
    free(ids);

    CHECK(classes_unlink_teacher(teacher_id, c.id) == SUCCESS);
    CHECK(classes_teacher_has_access(teacher_id, c.id) == 0);

    database_shutdown();
    unlink(tmp);

    printf("\nPermissions/Classes: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
