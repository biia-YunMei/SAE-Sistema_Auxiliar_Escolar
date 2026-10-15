/* Consulta anônima de leitura sem código; publicações continuam restritas. */
#define _POSIX_C_SOURCE 200809L
#include "whatsapp.h"
#include "database.h"
#include "classes.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failed;
#define CHECK(expr) do { if (!(expr)) { ++failed; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #expr); } } while (0)

static long count(const char *sql)
{
    sqlite3_stmt *st = NULL;
    if (db_prepare(sql, &st) != SUCCESS) { ++failed; return -1; }
    long result = sqlite3_step(st) == SQLITE_ROW ? (long)sqlite3_column_int64(st, 0) : -1;
    sqlite3_finalize(st);
    return result;
}

int main(void)
{
    char path[256];
    snprintf(path, sizeof path, "data/test_feira_%ld.db", (long)getpid());
    logger_init("logs", LOG_ERROR);
    if (database_init(path) != SUCCESS) return 1;
    Class a, b;
    CHECK(classes_create("9A", "9 ano", SHIFT_MORNING, &a) == SUCCESS);
    CHECK(classes_create("9B", "9 ano", SHIFT_MORNING, &b) == SUCCESS);
    /* Dados ficticios apenas no banco isolado do teste. */
    CHECK(db_exec("INSERT INTO users(id,name,phone,user_type) VALUES(901,'Prof teste','+5511999990901','PROF');"
                  "INSERT INTO teachers(id,user_id) VALUES(901,901);") == SUCCESS);
    char sql[1024];
    snprintf(sql, sizeof sql,
        "INSERT INTO tasks(teacher_id,class_id,subject,title,description,due_date) "
        "VALUES(901,%lld,'Matematica','Lista Feira A','Exercicios Feira A','2099-12-20');"
        "INSERT INTO notices(author_id,class_id,title,content,publish_date) "
        "VALUES(901,%lld,'Feira B','Aviso exclusivo B','2020-01-01');",
        (long long)a.id, (long long)b.id);
    CHECK(db_exec(sql) == SUCCESS);
    char reply[4001];
#define SAY(ref, text) CHECK(whatsapp_receive_anonymous_message(ref, text, reply, sizeof reply) == SUCCESS)
    SAY("visitor", "tarefas"); CHECK(strstr(reply, "De qual turma") != NULL);
    SAY("visitor", "9A"); CHECK(strstr(reply, "Exercicios Feira A") != NULL);
    SAY("visitor", "tarefas"); CHECK(strstr(reply, "Exercicios Feira A") != NULL);
    SAY("visitor", "avisos da turma 9B"); CHECK(strstr(reply, "Aviso exclusivo B") != NULL);
    char class_id_query[128];
    snprintf(class_id_query, sizeof class_id_query,
        "liste as atividades da turma com ID %lld", (long long)b.id);
    SAY("id_query", class_id_query);
    CHECK(strstr(reply, "Tarefas de 9B") != NULL);
    SAY("natural_query", "liste as atividades do 9º ano B");
    CHECK(strstr(reply, "Tarefas de 9B") != NULL);
    SAY("visitor", "tarefas da turma 99Z"); CHECK(strstr(reply, "cadastrada") != NULL);
    SAY("ambiguous", "tarefas do 9 ano"); CHECK(strstr(reply, "9A") && strstr(reply, "9B"));
    SAY("ambiguous", "9A"); CHECK(strstr(reply, "Exercicios Feira A") != NULL);
    SAY("cancel", "tarefas");
    SAY("cancel", "cancelar"); CHECK(strstr(reply, "cancelada") != NULL);
    SAY("visitor", "criar tarefa"); CHECK(strstr(reply, "autorizado") != NULL);
    CHECK(count("SELECT count(*) FROM tasks;") == 1);
    CHECK(count("SELECT count(*) FROM classes;") == 2);
    CHECK(count("SELECT count(*) FROM anonymous_students WHERE class_verified<>0;") == 0);
    SAY("new_visitor", "tarefas"); CHECK(strstr(reply, "De qual turma") != NULL);
    database_shutdown();
    unlink(path);
    char sidecar[280];
    snprintf(sidecar, sizeof sidecar, "%s-wal", path); unlink(sidecar);
    snprintf(sidecar, sizeof sidecar, "%s-shm", path); unlink(sidecar);
    printf("Feira: %d falhas\n", failed);
    return failed ? 1 : 0;
}
