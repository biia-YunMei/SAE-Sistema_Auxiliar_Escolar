/* test_tasks.c
 * Testes de CRUD de tarefas, regras de permissão e validações.
 */
#include "tasks.h"
#include "users.h"
#include "classes.h"
#include "database.h"
#include "logger.h"
#include "error.h"

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

    const char *tmp = "data/test_tasks.db";
    unlink(tmp); unlink("data/test_tasks.db-wal"); unlink("data/test_tasks.db-shm");
    CHECK(database_init(tmp) == SUCCESS);

    /* Setup: 2 professores, 1 turma */
    User p1, p2;
    users_create("Prof 1", "+5511911110001", USER_TYPE_PROF, "senhaProf1", &p1);
    users_create("Prof 2", "+5511911110002", USER_TYPE_PROF, "senhaProf2", &p2);
    Class c;
    CHECK(classes_create("3A", "3o ano", SHIFT_AFTERNOON, &c) == SUCCESS);

    /* Prof 1 vinculado, Prof 2 não */
    int64_t t1 = 0, t2 = 0;
    {
        sqlite3_stmt *st = NULL;
        db_prepare("SELECT id FROM teachers WHERE user_id=?;", &st);
        db_bind_int64(st, 1, (long long)p1.id); sqlite3_step(st);
        t1 = sqlite3_column_int64(st, 0); sqlite3_finalize(st);

        db_prepare("SELECT id FROM teachers WHERE user_id=?;", &st);
        db_bind_int64(st, 1, (long long)p2.id); sqlite3_step(st);
        t2 = sqlite3_column_int64(st, 0); sqlite3_finalize(st);
    }
    CHECK(t1 > 0 && t2 > 0);
    CHECK(classes_link_teacher(t1, c.id) == SUCCESS);

    /* Prof 1 cria tarefa */
    Task task;
    CHECK(tasks_create(t1, c.id, "Matemática", "Lista 1",
                       "Exs 1 a 10, p. 50", "2026-08-30", &task) == SUCCESS);
    CHECK(task.id > 0);
    CHECK(strcmp(task.subject, "Matemática") == 0);
    CHECK(strcmp(task.due_date, "2026-08-30") == 0);

    /* Prof 2 tenta criar tarefa na turma do Prof 1: proibido */
    CHECK(tasks_create(t2, c.id, "História", "Trabalho",
                       "", "2026-09-01", &task) == ERROR_FORBIDDEN);

    /* Data inválida */
    CHECK(tasks_create(t1, c.id, "X", "Y", "z", "31-12-2026", &task)
          == ERROR_INVALID_DATE);
    CHECK(tasks_create(t1, c.id, "X", "Y", "z", "2026-13-01", &task)
          == ERROR_INVALID_DATE);

    /* Edição: Prof 2 tenta editar tarefa do Prof 1: proibido */
    CHECK(tasks_update(task.id, t2, /*is_coord=*/false,
                       "Matemática", "Lista 1 (rev)",
                       "Exs 1 a 12, p. 50", "2026-08-30")
          == ERROR_FORBIDDEN);

    /* Prof 1 edita com sucesso */
    CHECK(tasks_update(task.id, t1, false,
                       "Matemática", "Lista 1 (rev)",
                       "Exs 1 a 12, p. 50", "2026-08-30") == SUCCESS);

    /* Coord edita tarefa alheia: permitido */
    CHECK(tasks_update(task.id, 9999, /*is_coord=*/true,
                       "Matemática", "Lista 1 (oficial)",
                       "Definido pela coordenação", "2026-08-30") == SUCCESS);

    /* Exclusão: Prof 2 não pode; Prof 1 pode */
    CHECK(tasks_delete(task.id, t2, false) == ERROR_FORBIDDEN);
    CHECK(tasks_delete(task.id, t1, false) == SUCCESS);
    CHECK(tasks_find_by_id(task.id, &task) == ERROR_NOT_FOUND);

    /* Listagem */
    Task t2row;
    tasks_create(t1, c.id, "Matemática", "Lista 2", "", "2026-09-05", &t2row);
    tasks_create(t1, c.id, "Português",  "Redação", "", "2026-09-06", &t2row);

    Task *list = NULL; size_t n = 0;
    CHECK(tasks_list_by_class(c.id, false, &list, &n) == SUCCESS);
    CHECK(n == 2);
    free(list);

    CHECK(tasks_list_by_teacher(t1, true, &list, &n) == SUCCESS);
    CHECK(n == 2);
    free(list);

    CHECK(tasks_list_all(false, &list, &n) == SUCCESS);
    CHECK(n >= 1);
    free(list);

    database_shutdown();
    unlink(tmp);
    printf("\nTasks: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
