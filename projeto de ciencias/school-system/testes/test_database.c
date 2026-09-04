/* test_database.c
 * Testes da camada de banco. Cria um arquivo temporário e executa
 * migrations, inserts e queries parametrizadas.
 */
#include "database.h"
#include "error.h"
#include "logger.h"
#include "validation.h"

#include <assert.h>
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

    const char *tmp = "data/test_school.db";
    unlink(tmp);
    unlink("data/test_school.db-wal");
    unlink("data/test_school.db-shm");

    int rc = database_init(tmp);
    CHECK(rc == SUCCESS);

    /* Verifica que tabelas foram criadas */
    sqlite3_stmt *st = NULL;
    CHECK(db_prepare(
        "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name;", &st) == SUCCESS);

    int found_users = 0, found_tasks = 0, found_notices = 0, found_convs = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(st, 0);
        if (strcmp(name, "users") == 0)         found_users = 1;
        if (strcmp(name, "tasks") == 0)         found_tasks = 1;
        if (strcmp(name, "notices") == 0)       found_notices = 1;
        if (strcmp(name, "conversations") == 0) found_convs  = 1;
    }
    sqlite3_finalize(st);
    CHECK(found_users);
    CHECK(found_tasks);
    CHECK(found_notices);
    CHECK(found_convs);

    /* Inserção parametrizada */
    CHECK(db_exec("INSERT INTO users(name, phone, user_type) VALUES "
                  "('João','+5511999998888','COORD');") == SUCCESS);

    sqlite3_stmt *q = NULL;
    CHECK(db_prepare("SELECT user_type FROM users WHERE phone=?;", &q) == SUCCESS);
    CHECK(db_bind_text(q, 1, "+5511999998888") == SUCCESS);
    int ok = (sqlite3_step(q) == SQLITE_ROW);
    CHECK(ok);
    if (ok) {
        const unsigned char *t = sqlite3_column_text(q, 0);
        CHECK(t && strcmp((const char *)t, "COORD") == 0);
    }
    sqlite3_finalize(q);

    /* Inicializar duas vezes não pode corromper */
    CHECK(database_init(tmp) == SUCCESS);

    database_shutdown();
    unlink(tmp);

    printf("\nDatabase: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
