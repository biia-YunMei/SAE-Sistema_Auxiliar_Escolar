/* test_auth.c
 * Testes de auth, hash de senha, validação de telefone.
 */
#include "users.h"
#include "auth.h"
#include "database.h"
#include "validation.h"
#include "logger.h"

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

    const char *tmp = "data/test_auth.db";
    unlink(tmp); unlink("data/test_auth.db-wal"); unlink("data/test_auth.db-shm");
    CHECK(database_init(tmp) == SUCCESS);

    /* Hash deve ser determinístico-para-mesmo-salt: comparamos dois hashes
     * independentes do mesmo password — devem diferir pelo salt, mas
     * ambos devem verificar. */
    char h1[128], h2[128];
    CHECK(users_hash_password("segredo123", h1, sizeof h1) == SUCCESS);
    CHECK(users_hash_password("segredo123", h2, sizeof h2) == SUCCESS);
    CHECK(strcmp(h1, h2) != 0);              /* salts diferentes */
    CHECK(users_verify_password("segredo123", h1) == SUCCESS);
    CHECK(users_verify_password("errado",    h1) == ERROR_UNAUTHORIZED);

    /* Criação de usuários com validação */
    User u;
    CHECK(users_create("Coord A", "+5511999990001", USER_TYPE_COORD, "senhaSegura", &u)
          == SUCCESS);
    CHECK(u.id > 0);
    CHECK(u.type == USER_TYPE_COORD);

    CHECK(users_create("Prof B",  "+5511999990002", USER_TYPE_PROF,  "senhaProf", &u)
          == SUCCESS);
    int64_t teacher_user_id = u.id;
    CHECK(users_create("Aluno C", "+5511999990003", USER_TYPE_STUD,  "senhaAluno", &u)
          == SUCCESS);
    int64_t student_user_id = u.id;

    /* Telefone inválido */
    CHECK(users_create("X", "abc", USER_TYPE_STUD, NULL, &u)
          == ERROR_INVALID_PHONE);
    CHECK(users_create("X", "123", USER_TYPE_STUD, NULL, &u)
          == ERROR_INVALID_PHONE);

    /* Telefone duplicado */
    CHECK(users_create("Dup", "+5511999990001", USER_TYPE_STUD, NULL, &u)
          == ERROR_ALREADY_EXISTS);

    /* Auth por telefone */
    AuthSession s;
    CHECK(auth_login_phone("+5511999990001", "senhaSegura", &s) == SUCCESS);
    CHECK(s.user_id > 0);
    CHECK(s.type == USER_TYPE_COORD);
    CHECK(auth_login_phone("+5511999990001", "errada", &s) == ERROR_UNAUTHORIZED);
    CHECK(auth_login_phone("+5511000000000", NULL,    &s) == ERROR_UNAUTHORIZED);
    CHECK(auth_login_phone("+5511999990002", NULL, &s) == ERROR_UNAUTHORIZED);
    CHECK(auth_login_phone("+5511999990002", "senhaProf", &s) == SUCCESS);
    CHECK(s.type == USER_TYPE_PROF);
    CHECK(auth_login_phone("+5511999990003", "senhaAluno", &s) == ERROR_UNAUTHORIZED);

    sqlite3_stmt *student_query = NULL;
    CHECK(db_prepare("SELECT grade FROM students WHERE user_id=?;", &student_query) == SUCCESS);
    db_bind_int64(student_query, 1, (long long)student_user_id);
    CHECK(sqlite3_step(student_query) == SQLITE_ROW);
    CHECK(strcmp((const char *)sqlite3_column_text(student_query, 0), "") == 0);
    sqlite3_finalize(student_query);

    /* Token */
    char token[80];
    CHECK(auth_session_create(&s, token, sizeof token) == SUCCESS);
    CHECK(strlen(token) >= 32);
    AuthSession r;
    CHECK(auth_session_resolve(token, &r) == SUCCESS);
    CHECK(r.user_id == s.user_id);
    CHECK(auth_session_revoke(token) == SUCCESS);
    CHECK(auth_session_resolve(token, &r) == ERROR_UNAUTHORIZED);

    CHECK(auth_session_create(&s, token, sizeof token) == SUCCESS);
    CHECK(users_delete(teacher_user_id) == SUCCESS);
    CHECK(auth_session_resolve(token, &r) == ERROR_UNAUTHORIZED);

    database_shutdown();
    unlink(tmp);

    printf("\nAuth: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
