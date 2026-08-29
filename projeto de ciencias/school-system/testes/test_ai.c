/* test_ai.c
 * Testes da camada de IA.
 */
#include "ai.h"
#include "logger.h"
#include "validation.h"

#include <stdio.h>
#include <string.h>

static int passed = 0, failed = 0;
#define CHECK(expr) do { \
    if (expr) { passed++; } \
    else { failed++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #expr); } \
} while (0)

int main(void)
{
    logger_init("logs", LOG_ERROR);

    /* JSON extraction */
    const char *j = "{\"foo\":\"bar\",\"nested\":{\"x\":\"y\"}}";
    char buf[32];
    CHECK(ai_minimal_json_extract(j, "foo", buf, sizeof buf));
    CHECK(strcmp(buf, "bar") == 0);
    CHECK(!ai_minimal_json_extract(j, "nope", buf, sizeof buf));

    /* Parse da mensagem do professor */
    TaskData t;
    int rc = ai_parse_teacher_message(
        "Turma 2A, atividade de matemática sobre funções. "
        "Página 120, exercícios 1 a 10. Entrega 2026-08-28.",
        &t);
    CHECK(rc == SUCCESS);
    CHECK(strcmp(t.tipo, "tarefa") == 0);
    CHECK(strcmp(t.turma, "2A") == 0);
    CHECK(strcmp(t.disciplina, "Matemática") == 0);
    CHECK(strcmp(t.data_entrega, "2026-08-28") == 0);

    /* Mensagem sem turma → erro */
    rc = ai_parse_teacher_message("Atividade de história páginas 50-55", &t);
    CHECK(rc == ERROR_INVALID_INPUT);

    /* Mensagem com data inválida (impossível afirmar "31-13-2026" no ISO) */
    rc = ai_parse_teacher_message("Turma 3A, português, entrega 31-13-2026", &t);
    /* Aqui só setamos data[0]=0 porque find_iso_date não casa → ERROR_INVALID_INPUT */
    CHECK(rc != SUCCESS);

    /* Pergunta do aluno */
    char resp[2048];
    rc = ai_answer_student_question("Como faço esse exercício?",
        "Funções: domínio, imagem e composição.",
        resp, sizeof resp);
    CHECK(rc == SUCCESS);
    CHECK(strlen(resp) > 0);
    CHECK(strstr(resp, "etapas") != NULL);

    /* Sem contexto → mensagem honesta */
    rc = ai_answer_student_question("O que é tal coisa?", NULL, resp, sizeof resp);
    CHECK(rc == SUCCESS);
    CHECK(strstr(resp, "Não encontrei") != NULL);

    printf("\nAI: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
