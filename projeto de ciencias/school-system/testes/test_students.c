/* Testes do cadastro administrativo de alunos, sem servidor de sockets. */
#define _POSIX_C_SOURCE 200809L
#include "auth.h"
#include "classes.h"
#include "database.h"
#include "http_server.h"
#include "logger.h"
#include "users.h"
#include "controllers.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>

static int passed, failed;
#define CHECK(expr) do { \
    if (expr) ++passed; \
    else { ++failed; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #expr); } \
} while (0)

int http_respond_json(HttpResponse *res, int status, const char *json)
{
    if (!res || !json || strlen(json) >= sizeof res->body_storage) return ERROR_INVALID_INPUT;
    snprintf(res->body_storage, sizeof res->body_storage, "%s", json);
    res->body = res->body_storage;
    res->body_len = strlen(res->body_storage);
    res->status = status;
    res->content_type = "application/json";
    return SUCCESS;
}

int http_respond_error(HttpResponse *res, int code, const char *message)
{
    char json[512];
    snprintf(json, sizeof json, "{\"error\":%d,\"message\":\"%s\"}", code, message ? message : "");
    return http_respond_json(res, code, json);
}

int http_respond_text(HttpResponse *res, int status, const char *text)
{
    return http_respond_json(res, status, text ? text : "");
}

static int make_token(UserType type, int64_t user_id, char token[80])
{
    AuthSession session = {0};
    session.user_id = user_id;
    session.type = type;
    session.expires_at = time(NULL) + 3600;
    return auth_session_create(&session, token, 80);
}

int main(void)
{
    const char *path = "data/test_students.db";
    unlink(path); unlink("data/test_students.db-wal"); unlink("data/test_students.db-shm");
    logger_init("logs", LOG_ERROR);
    CHECK(database_init(path) == SUCCESS);
    setenv("SAE_BOOTSTRAP_KEY", "test-setup-key-with-at-least-32-characters-long", 1);

    HttpRequest request = {0};
    HttpResponse response = {0};
    request.method = "GET";
    CHECK(ctrl_bootstrap_status(&request, &response) == SUCCESS);
    CHECK(response.status == 200 && strstr(response.body, "\"available\":true") != NULL);
    memset(&response, 0, sizeof response);
    request.method = "POST";
    request.body = "{\"name\":\"Coord\",\"phone\":\"+5511999000001\",\"password\":\"senhaSegura1!\",\"bootstrap_key\":\"incorreta\"}";
    CHECK(ctrl_bootstrap_admin(&request, &response) == SUCCESS);
    CHECK(response.status == 403);
    memset(&response, 0, sizeof response);
    request.body = "{\"name\":\"Coord\",\"phone\":\"+5511999000001\",\"password\":\"senhaSegura1!\",\"bootstrap_key\":\"test-setup-key-with-at-least-32-characters-long\"}";
    CHECK(ctrl_bootstrap_admin(&request, &response) == SUCCESS);
    CHECK(response.status == 201);
    memset(&response, 0, sizeof response);
    request.method = "GET";
    CHECK(ctrl_bootstrap_status(&request, &response) == SUCCESS);
    CHECK(response.status == 200 && strstr(response.body, "\"available\":false") != NULL);
    request.method = "POST";
    User coordinator;
    CHECK(users_find_by_phone("+5511999000001", &coordinator) == SUCCESS);
    AuthSession coordinator_session;
    CHECK(auth_login_phone("+5511999000001", "senhaSegura1!", &coordinator_session) == SUCCESS);
    char token[80] = {0};
    CHECK(make_token(USER_TYPE_COORD, coordinator.id, token) == SUCCESS);
    Class class_a, class_b;
    CHECK(classes_create("8A", "8º ano", SHIFT_MORNING, &class_a) == SUCCESS);
    CHECK(classes_create("8B", "9º ano", SHIFT_MORNING, &class_b) == SUCCESS);

    User professor;
    CHECK(users_create("Professor", "+5511999000002", USER_TYPE_PROF, "senhaProf", &professor) == SUCCESS);
    char professor_token[80] = {0};
    CHECK(make_token(USER_TYPE_PROF, professor.id, professor_token) == SUCCESS);
    request.auth = professor_token;
    request.body = "{\"name\":\"Intruso\",\"phone\":\"+5511999000003\",\"type\":\"COORD\"}";
    memset(&response, 0, sizeof response);
    CHECK(ctrl_users_create(&request, &response) == ERROR_FORBIDDEN);
    CHECK(response.status == 403);
    request.body = "{\"name\":\"Outro\",\"whatsapp\":\"+5511999000004\"}";
    memset(&response, 0, sizeof response);
    CHECK(ctrl_teachers_create(&request, &response) == ERROR_FORBIDDEN);
    CHECK(response.status == 403);
    memset(&response, 0, sizeof response);
    CHECK(ctrl_classes_add_teacher(&request, &response, "1") == ERROR_FORBIDDEN);
    CHECK(response.status == 403);

    request.auth = token;
    request.body = "{\"name\":\"Outro\",\"phone\":\"+5511999000099\",\"password\":\"senhaSegura2!\",\"bootstrap_key\":\"test-setup-key-with-at-least-32-characters-long\"}";
    memset(&response, 0, sizeof response);
    CHECK(ctrl_bootstrap_admin(&request, &response) == SUCCESS);
    CHECK(response.status == 409);

    request.method = "POST";
    request.auth = token;
    char student_body[256];
    snprintf(student_body, sizeof student_body,
        "{\"name\":\"Ana Souza\",\"phone\":\"(11) 98888-7777\",\"class_id\":%lld}",
        (long long)class_a.id);
    request.body = student_body;
    CHECK(ctrl_students_create(&request, &response) == SUCCESS);
    CHECK(response.status == 201);
    CHECK(strstr(response.body, "\"name\":\"Ana Souza\"") != NULL);
    CHECK(strstr(response.body, "\"phone\":\"+5511988887777\"") != NULL);
    CHECK(strstr(response.body, "\"grade\":\"8º ano\"") != NULL);
    CHECK(strstr(response.body, "\"class_id\":") != NULL);

    request.method = "GET";
    request.body = "";
    memset(&response, 0, sizeof response);
    CHECK(ctrl_students_list(&request, &response) == SUCCESS);
    CHECK(response.status == 200);
    CHECK(strstr(response.body, "Ana Souza") != NULL);
    CHECK(strstr(response.body, "8º ano") != NULL);
    CHECK(strstr(response.body, "8A") != NULL);

    User student;
    CHECK(users_find_by_phone("+5511988887777", &student) == SUCCESS);
    request.method = "POST";
    char assign_body[128];
    snprintf(assign_body, sizeof assign_body,
        "{\"user_id\":%lld,\"class_id\":%lld}",
        (long long)student.id, (long long)class_b.id);
    request.body = assign_body;
    memset(&response, 0, sizeof response);
    CHECK(ctrl_students_assign_class(&request, &response) == SUCCESS);
    CHECK(response.status == 200);
    request.method = "GET";
    request.body = "";
    memset(&response, 0, sizeof response);
    CHECK(ctrl_students_list(&request, &response) == SUCCESS);
    CHECK(strstr(response.body, "9º ano") != NULL);
    CHECK(strstr(response.body, "8B") != NULL);

    sqlite3_stmt *notice = NULL;
    CHECK(db_prepare("INSERT INTO notices(author_id,class_id,title,content) VALUES(?,?,?,?);", &notice) == SUCCESS);
    db_bind_int64(notice, 1, coordinator.id);
    db_bind_int64(notice, 2, class_a.id);
    db_bind_text(notice, 3, "Aviso turma A");
    db_bind_text(notice, 4, "Conteudo privado A");
    CHECK(sqlite3_step(notice) == SQLITE_DONE);
    sqlite3_finalize(notice);
    CHECK(db_prepare("INSERT INTO notices(author_id,class_id,title,content) VALUES(?,?,?,?);", &notice) == SUCCESS);
    db_bind_int64(notice, 1, coordinator.id);
    db_bind_int64(notice, 2, class_b.id);
    db_bind_text(notice, 3, "Aviso turma B");
    db_bind_text(notice, 4, "Conteudo privado B");
    CHECK(sqlite3_step(notice) == SQLITE_DONE);
    sqlite3_finalize(notice);
    char student_token[80] = {0};
    CHECK(make_token(USER_TYPE_STUD, student.id, student_token) == SUCCESS);
    request.auth = student_token;
    request.method = "GET";
    request.body = "";
    memset(&response, 0, sizeof response);
    CHECK(ctrl_notices_list(&request, &response) == SUCCESS);
    CHECK(response.status == 200);
    CHECK(strstr(response.body, "Aviso turma B") != NULL);
    CHECK(strstr(response.body, "Aviso turma A") == NULL);

    request.method = "POST";
    request.auth = token;
    snprintf(student_body, sizeof student_body,
        "{\"name\":\"Outra Aluna\",\"phone\":\"11988887777\",\"class_id\":%lld}",
        (long long)class_a.id);
    request.body = student_body;
    memset(&response, 0, sizeof response);
    CHECK(ctrl_students_create(&request, &response) == SUCCESS);
    CHECK(response.status == 409);

    request.auth = NULL;
    memset(&response, 0, sizeof response);
    CHECK(ctrl_students_create(&request, &response) == SUCCESS);
    CHECK(response.status == 401);

    database_shutdown();
    unlink(path); unlink("data/test_students.db-wal"); unlink("data/test_students.db-shm");
    printf("\nStudents: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
