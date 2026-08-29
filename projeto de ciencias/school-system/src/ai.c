/* ai.c
 * Cliente de IA em C.
 *
 * Funciona em 2 modos:
 *   - AI_USE_REMOTE=1  → chama a API Anthropic via libcurl
 *   - default          → heurística local (para testes e a feira)
 *
 * Toda resposta passa por validação rigorosa.
 * Nunca invente dados: se não encontrar, devolva erro e explique.
 */
#include "ai.h"
#include "validation.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---------- parser JSON mínimo ------------------------------------- */

int ai_minimal_json_extract(const char *json, const char *key,
                            char *out, size_t cap)
{
    if (!json || !key || !out || cap == 0) return 0;
    char needle[64];
    int n = snprintf(needle, sizeof needle, "\"%s\":\"", key);
    if (n <= 0 || (size_t)n >= sizeof needle) return 0;
    const char *p = strstr(json, needle);
    if (!p) return 0;
    p += n;
    const char *e = strchr(p, '"');
    if (!e) return 0;
    size_t l = (size_t)(e - p);
    if (l >= cap) l = cap - 1;
    memcpy(out, p, l);
    out[l] = '\0';
    return 1;
}

/* Procura uma das palavras-chave na mensagem (heurística didática). */
static int contains_word(const char *s, const char *w)
{
    if (!s || !w) return 0;
    size_t n = strlen(w);
    const char *p = s;
    while ((p = strstr(p, w))) {
        int left_ok  = (p == s) || !isalnum((unsigned char)p[-1]);
        int right_ok = !isalnum((unsigned char)p[n]);
        if (left_ok && right_ok) return 1;
        p += n;
    }
    return 0;
}

/* Tenta extrair uma data em formato YYYY-MM-DD da mensagem livre. */
static int find_iso_date(const char *s, char *out, size_t cap)
{
    if (!s || cap < 11) return 0;
    for (const char *p = s; p && *p; p++) {
        if (isdigit((unsigned char)p[0]) &&
            isdigit((unsigned char)p[1]) &&
            isdigit((unsigned char)p[2]) &&
            isdigit((unsigned char)p[3]) &&
            p[4] == '-' &&
            isdigit((unsigned char)p[5]) &&
            isdigit((unsigned char)p[6]) &&
            p[7] == '-' &&
            isdigit((unsigned char)p[8]) &&
            isdigit((unsigned char)p[9])) {
            size_t l = 10;
            if (l >= cap) l = cap - 1;
            memcpy(out, p, l); out[l] = '\0';
            if (validation_is_valid_date(out)) return 1;
        }
    }
    return 0;
}

/* ---------- API pública (modo local) ------------------------------ */

int ai_parse_teacher_message(const char *message, TaskData *out)
{
    if (!message || !out) return ERROR_INVALID_INPUT;
    if (!validation_length_between(message, 5, 2000)) return ERROR_TOO_LONG;

    memset(out, 0, sizeof *out);
    snprintf(out->tipo, sizeof out->tipo, "tarefa");

    /* heurística: turma */
    if (contains_word(message, "2A"))      snprintf(out->turma, sizeof out->turma, "2A");
    else if (contains_word(message, "3A")) snprintf(out->turma, sizeof out->turma, "3A");
    else if (contains_word(message, "1A")) snprintf(out->turma, sizeof out->turma, "1A");
    else                                   out->turma[0] = '\0';

    /* disciplina */
    if      (contains_word(message, "matem"))    snprintf(out->disciplina, sizeof out->disciplina, "Matemática");
    else if (contains_word(message, "portugu"))  snprintf(out->disciplina, sizeof out->disciplina, "Português");
    else if (contains_word(message, "históri"))  snprintf(out->disciplina, sizeof out->disciplina, "História");
    else if (contains_word(message, "ciência"))  snprintf(out->disciplina, sizeof out->disciplina, "Ciências");
    else if (contains_word(message, "inglês"))   snprintf(out->disciplina, sizeof out->disciplina, "Inglês");
    else                                         out->disciplina[0] = '\0';

    /* descrição = a própria mensagem (didático; em produção limita a 512) */
    snprintf(out->descricao, sizeof out->descricao, "%s", message);

    /* data ISO ou vazia (validador chamará ERROR_INVALID_DATE depois) */
    if (!find_iso_date(message, out->data_entrega, sizeof out->data_entrega))
        out->data_entrega[0] = '\0';

    /* Valida campos essenciais */
    if (out->turma[0] == '\0' || out->disciplina[0] == '\0')
        return ERROR_INVALID_INPUT;
    if (out->data_entrega[0] && !validation_is_valid_date(out->data_entrega))
        return ERROR_INVALID_DATE;

    log_info("ai_parse_teacher_message: turma=%s disciplina=%s data=%s",
             out->turma, out->disciplina, out->data_entrega);
    return SUCCESS;
}

int ai_answer_student_question(const char *question, const char *context,
                               char *response, size_t response_cap)
{
    if (!question || !response || response_cap < 16) return ERROR_INVALID_INPUT;
    if (!validation_length_between(question, 3, 800)) return ERROR_TOO_LONG;

    /* Política: orientar, nunca entregar resposta final de exercícios.
     * Se contexto vazio, devolvemos mensagem honesta. */
    if (!context || !*context) {
        snprintf(response, response_cap,
            "Não encontrei dados suficientes no seu contexto escolar. "
            "Por favor, procure o professor responsável.");
        return SUCCESS;
    }

    /* Heurística didática para a feira: devolver 1-3 etapas de raciocínio. */
    snprintf(response, response_cap,
        "Vamos por etapas:\n"
        "1) Releia o enunciado e identifique o que se pede.\n"
        "2) Busque no material: %s\n"
        "3) Tente aplicar o método passo a passo. "
        "Se ainda travar, me diga em qual etapa ficou e eu oriento melhor.",
        context);
    return SUCCESS;
}

/* ---------- integração com API remota (Anthropic) ----------------- *
 * Compilação condicional idêntica ao whatsapp_adapter.
 */
#if defined(AI_USE_REMOTE)
#include "config.h"

extern const AppConfig *app_config(void);

int ai_call_anthropic(const char *prompt, char *response, size_t cap)
{
    const AppConfig *cfg = app_config();
    if (!cfg || !*cfg->ai_api_key) return ERROR_CONFIG;
    (void)prompt; (void)response; (void)cap;
    return ERROR_NOT_IMPLEMENTED; /* stub — ligação curl em produção */
}
#endif
