/* ai.h
 * Camada de Inteligência Artificial.
 *
 * Toda chamada externa é isolada aqui. O back-end envia um *prompt*
 * e recebe dados estruturados validados — jamais confiar cegamente
 * no conteúdo retornado.
 *
 * Funcionalidades:
 *   - ai_parse_teacher_message   → extrai campos para uma TaskData
 *   - ai_answer_student_question → responde perguntas com base no contexto
 *
 * Validações obrigatórias:
 *   - Limite de tamanho de entrada e saída.
 *   - Resposta JSON deve ser bem-formada (mínimo).
 *   - Campos obrigatórios presentes.
 *   - Não inventar dados fora do contexto fornecido.
 */
#ifndef SCHOOL_AI_H
#define SCHOOL_AI_H

#include "error.h"
#include <stddef.h>

typedef struct {
    char tipo[16];          /* "tarefa" */
    char turma[32];
    char disciplina[64];
    char descricao[512];
    char data_entrega[16];  /* YYYY-MM-DD */
} TaskData;

int ai_parse_teacher_message(const char *message, TaskData *out);
int ai_answer_student_question(const char *question, const char *context,
                               char *response, size_t response_cap);
/* Normaliza Markdown/LaTeX básico para mensagens de texto, como WhatsApp. */
void ai_format_student_response(char *response, size_t response_cap);
/* Indica se a chave e o modelo remoto estão configurados no processo. */
int ai_remote_enabled(void);

/* Utilidade interna exposta para testes. */
int ai_minimal_json_extract(const char *json, const char *key,
                            char *out, size_t cap);

#endif /* SCHOOL_AI_H */
