/* validation.h
 * Validadores reusáveis (telefone, data, comprimento, etc).
 * Mantidos puros e testáveis.
 */
#ifndef SCHOOL_VALIDATION_H
#define SCHOOL_VALIDATION_H

#include <stdbool.h>
#include <stddef.h>

/* Valida número de telefone. Aceita formatos:
 *   +55XXXXXXXXXXX
 *   55XXXXXXXXXXX
 *   0XX9XXXXXXXX    (com ou sem 0 inicial)
 * Retorna true se for plausível (8 a 15 dígitos após o + opcional). */
bool validation_is_valid_phone(const char *phone);

/* Valida data ISO 8601 (YYYY-MM-DD). */
bool validation_is_valid_date(const char *date);

/* Valida hora HH:MM (24h). */
bool validation_is_valid_time(const char *time);

/* Verifica se a string contém apenas os caracteres permitidos
 * (letras, números, espaço, acentos comuns e pontuação escolar). */
bool validation_is_safe_text(const char *text);

/* Verifica limites de comprimento. */
bool validation_length_between(const char *s, size_t min, size_t max);

/* Sanitiza removendo caracteres de controle. Retorna número de bytes
 * escritos (excluindo terminador). */
size_t validation_sanitize_text(char *dst, size_t cap, const char *src);

#endif /* SCHOOL_VALIDATION_H */
