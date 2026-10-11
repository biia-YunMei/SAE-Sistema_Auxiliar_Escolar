/* test_whatsapp.c
 * Testes da camada de abstração WhatsApp.
 */
#include "whatsapp.h"
#include "validation.h"
#include "logger.h"
#include "database.h"
#include "users.h"
#include "classes.h"

#include <stdio.h>
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
    const char *db_path = "data/test_whatsapp.db";
    unlink(db_path); unlink("data/test_whatsapp.db-wal"); unlink("data/test_whatsapp.db-shm");
    CHECK(database_init(db_path) == SUCCESS);
    CHECK(whatsapp_init() == SUCCESS);

    /* Telefone inválido */
    CHECK(whatsapp_send_message("123",        "oi")      == ERROR_INVALID_PHONE);
    CHECK(whatsapp_send_message("+abcdefg",   "oi")      == ERROR_INVALID_PHONE);
    CHECK(whatsapp_send_message(NULL,         "oi")      == ERROR_INVALID_PHONE);
    CHECK(whatsapp_send_message("+551199999", "oi")      == SUCCESS);
    CHECK(whatsapp_send_message("+551199999", "")        == ERROR_INVALID_INPUT);
    /* Texto demasiado grande deve ser rejeitado pelo stub */
    {
        char big[5000];
        memset(big, 'a', sizeof big - 1);
        big[sizeof big - 1] = '\0';
        CHECK(whatsapp_send_message("+551199999", big) == ERROR_TOO_LONG);
    }

    /* Receive */
    char reply[256];
    CHECK(whatsapp_receive_message("+551199999", "olá",
                                   reply, sizeof reply) == SUCCESS);
    CHECK(strstr(reply, "nao esta cadastrado como aluno") != NULL);

    CHECK(whatsapp_receive_message("12345",      "olá", reply, sizeof reply)
          == ERROR_INVALID_PHONE);

    Class class_9a;
    CHECK(classes_create("9A", "9º ano", SHIFT_MORNING, &class_9a) == SUCCESS);
    sqlite3_stmt *statement = NULL;
    User teacher;
    CHECK(users_create("Professora", "+5511999990001", USER_TYPE_PROF, "senhaProf", &teacher) == SUCCESS);
    int64_t teacher_id = 0;
    CHECK(db_prepare("SELECT id FROM teachers WHERE user_id=?;", &statement) == SUCCESS);
    db_bind_int64(statement, 1, teacher.id);
    CHECK(sqlite3_step(statement) == SQLITE_ROW);
    teacher_id = sqlite3_column_int64(statement, 0);
    sqlite3_finalize(statement);
    CHECK(classes_link_teacher(teacher_id, class_9a.id) == SUCCESS);
    Class class_9b;
    CHECK(classes_create("9B", "9º ano", SHIFT_MORNING, &class_9b) == SUCCESS);
    CHECK(whatsapp_receive_message(teacher.phone,
          "9B matematica pagina 2 entrega sexta", reply, sizeof reply) == SUCCESS);
    CHECK(strstr(reply, "nao esta autorizado") != NULL);
    CHECK(whatsapp_receive_message(teacher.phone,
          "9A matematica pagina 32 entrega sexta", reply, sizeof reply) == SUCCESS);
    CHECK(strstr(reply, "Resumo:") != NULL);
    CHECK(whatsapp_receive_message(teacher.phone, "SIM", reply, sizeof reply) == SUCCESS);
    CHECK(strstr(reply, "publicada") != NULL);
    CHECK(whatsapp_receive_message(teacher.phone,
          "9A matematica pagina 40 entrega sexta", reply, sizeof reply) == SUCCESS);
    CHECK(strstr(reply, "Resumo:") != NULL);
    CHECK(whatsapp_receive_message(teacher.phone,
          "9A matematica pagina 41 entrega sexta", reply, sizeof reply) == SUCCESS);
    CHECK(strstr(reply, "aguardando confirmacao") != NULL);
    CHECK(whatsapp_receive_message(teacher.phone, "CANCELAR", reply, sizeof reply) == SUCCESS);
    CHECK(strstr(reply, "cancelada") != NULL);

    const char *student = "+5511999990002";
    User student_user;
    CHECK(users_create("Aluno", student, USER_TYPE_STUD, NULL, &student_user) == SUCCESS);
    CHECK(db_prepare("UPDATE students SET class_id=?,grade=? WHERE user_id=?;", &statement) == SUCCESS);
    db_bind_int64(statement, 1, class_9a.id);
    db_bind_text(statement, 2, "9º ano");
    db_bind_int64(statement, 3, student_user.id);
    CHECK(sqlite3_step(statement) == SQLITE_DONE);
    sqlite3_finalize(statement);
    CHECK(whatsapp_receive_message(student, "Tem tarefa?", reply, sizeof reply) == SUCCESS);
    CHECK(strstr(reply, "Tarefas de 9A") != NULL);
    CHECK(whatsapp_receive_message(student, "9A", reply, sizeof reply) == SUCCESS);
    CHECK(strstr(reply, "Tarefas de 9A") == NULL);
    CHECK(whatsapp_receive_message(student, "2B tem tarefa?", reply, sizeof reply) == SUCCESS);
    CHECK(strstr(reply, "Tarefas de 9A") != NULL);
    User coordinator;
    CHECK(users_create("Coordenação", "+5511999990004", USER_TYPE_COORD, "senhaSegura", &coordinator) == SUCCESS);
    CHECK(whatsapp_receive_message(coordinator.phone,
          "COMUNICADO: Prova de Portugues dia 10 de outubro", reply, sizeof reply) == SUCCESS);
    CHECK(strstr(reply, "qual turma") != NULL);
    CHECK(whatsapp_receive_message(coordinator.phone, "TODAS", reply, sizeof reply) == SUCCESS);
    CHECK(strstr(reply, "publicado") != NULL);
    CHECK(db_prepare("INSERT INTO notices(author_id,class_id,title,content,publish_date) VALUES(?,?,?,?,'2999-12-31');", &statement) == SUCCESS);
    db_bind_int64(statement, 1, coordinator.id);
    db_bind_int64(statement, 2, class_9a.id);
    db_bind_text(statement, 3, "Comunicado futuro");
    db_bind_text(statement, 4, "Ainda nao publicado");
    CHECK(sqlite3_step(statement) == SQLITE_DONE);
    sqlite3_finalize(statement);
    CHECK(whatsapp_receive_message(student, "comunicados", reply, sizeof reply) == SUCCESS);
    CHECK(strstr(reply, "Prova de Portugues") != NULL);
    const char *from_ref = "snd_test_01";
    CHECK(whatsapp_receive_anonymous_message(from_ref, "Quais tarefas eu tenho?",
          reply, sizeof reply) == SUCCESS);
    CHECK(strstr(reply, "De qual turma") != NULL);
    CHECK(whatsapp_receive_anonymous_message(from_ref, "9A", reply, sizeof reply) == SUCCESS);
    CHECK(strstr(reply, "Turma registrada: 9A") != NULL);
    CHECK(strstr(reply, "Tarefas de 9A") != NULL);
    CHECK(whatsapp_receive_anonymous_message(from_ref, "avisos", reply, sizeof reply) == SUCCESS);
    CHECK(strstr(reply, "Prova de Portugues") != NULL);
    CHECK(strstr(reply, "Comunicado futuro") == NULL);
    CHECK(whatsapp_store_anonymous_inbox_message("m_test_01", from_ref, "oi", "text") == SUCCESS);
    CHECK(whatsapp_store_anonymous_inbox_message("m_test_01", from_ref, "oi repetido", "text") == SUCCESS);
    CHECK(db_prepare("SELECT count(*) FROM messages WHERE external_id='m_test_01';", &statement) == SUCCESS);
    CHECK(sqlite3_step(statement) == SQLITE_ROW && sqlite3_column_int(statement, 0) == 1);
    sqlite3_finalize(statement);
    CHECK(whatsapp_mark_anonymous_inbox_message("m_test_01", "PROCESSED") == SUCCESS);
    CHECK(whatsapp_receive_anonymous_message("snd_test_02", "Quais avisos?", reply, sizeof reply) == SUCCESS);
    CHECK(strstr(reply, "De qual turma") != NULL);
    CHECK(whatsapp_receive_anonymous_message("snd_test_02", "99Z", reply, sizeof reply) == SUCCESS);
    CHECK(strstr(reply, "Nao encontrei uma turma ativa") != NULL);
    CHECK(whatsapp_receive_anonymous_message("snd_test_02", "cancelar", reply, sizeof reply) == SUCCESS);
    CHECK(strstr(reply, "cancelada") != NULL);
    CHECK(whatsapp_receive_anonymous_message("snd_test_03", "Quais deveres tenho?",
          reply, sizeof reply) == SUCCESS);
    CHECK(strstr(reply, "De qual turma") != NULL);
    CHECK(whatsapp_receive_anonymous_message("snd_test_03",
          "Quem foi Mendel e como ele ajudou nos estudos de genética?",
          reply, sizeof reply) == SUCCESS);
    CHECK(strstr(reply, "Essa turma nao esta cadastrada") == NULL);
    CHECK(strstr(reply, "Vamos por etapas") != NULL);
    CHECK(whatsapp_receive_anonymous_message("snd_test_03", "9º ano",
          reply, sizeof reply) == SUCCESS);
    CHECK(strstr(reply, "mais de uma turma") != NULL);
    CHECK(strstr(reply, "9A") != NULL && strstr(reply, "9B") != NULL);
    CHECK(whatsapp_receive_anonymous_message("snd_test_03", "sou do nono A",
          reply, sizeof reply) == SUCCESS);
    CHECK(strstr(reply, "Turma registrada: 9A") != NULL);
    CHECK(whatsapp_receive_anonymous_message("snd_test_04", "Quais avisos?",
          reply, sizeof reply) == SUCCESS);
    char class_id_reference[32];
    snprintf(class_id_reference, sizeof class_id_reference, "ID %lld", (long long)class_9b.id);
    CHECK(whatsapp_receive_anonymous_message("snd_test_04", class_id_reference,
          reply, sizeof reply) == SUCCESS);
    CHECK(strstr(reply, "Turma registrada: 9B") != NULL);
    CHECK(whatsapp_receive_anonymous_message("invalid ref", "oi", reply, sizeof reply) == ERROR_INVALID_INPUT);
    CHECK(whatsapp_receive_message("+5511999990003", "8B matematica pagina 2 entrega sexta",
          reply, sizeof reply) == SUCCESS);
    CHECK(strstr(reply, "nao esta cadastrado como aluno") != NULL);
    CHECK(whatsapp_receive_message("+5511999990005", "2B tem tarefa?", reply, sizeof reply) == SUCCESS);
    CHECK(strstr(reply, "nao esta cadastrado como aluno") != NULL);

    CHECK(whatsapp_receive_anonymous_message("snd_test_05",
          "lista para mim quais tarefas estão disponíveis para a turma 9A",
          reply, sizeof reply) == SUCCESS);
    CHECK(strstr(reply, "Tarefas de 9A") != NULL);
    CHECK(whatsapp_receive_anonymous_message("snd_test_06",
          "tarefas da turma 2", reply, sizeof reply) == SUCCESS);
    CHECK(strstr(reply, "Essa turma ainda não está cadastrada") != NULL);
    char school_context[1500];
    whatsapp_build_school_context(school_context, sizeof school_context);
    CHECK(strstr(school_context, "9A") != NULL);
    CHECK(strstr(school_context, "9B") != NULL);

    whatsapp_shutdown();
    database_shutdown();
    unlink(db_path); unlink("data/test_whatsapp.db-wal"); unlink("data/test_whatsapp.db-shm");
    printf("\nWhatsApp: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
