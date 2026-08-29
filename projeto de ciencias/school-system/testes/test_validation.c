/* test_validation.c
 * Testes do módulo de validação.
 * Compila e roda sem dependências externas.
 */
#include "validation.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int passed = 0, failed = 0;
#define CHECK(expr) do { \
    if (expr) { passed++; } \
    else { failed++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #expr); } \
} while (0)

static void test_phone(void)
{
    CHECK( validation_is_valid_phone("+5511999998888"));
    CHECK( validation_is_valid_phone("5511999998888"));
    CHECK( validation_is_valid_phone("11999998888"));
    CHECK(!validation_is_valid_phone(""));
    CHECK(!validation_is_valid_phone("abc"));
    CHECK(!validation_is_valid_phone("+5511"));
    CHECK(!validation_is_valid_phone("+5511abc9998888"));
    CHECK( validation_is_valid_phone("+123456789012345")); /* 15 dígitos */
    CHECK(!validation_is_valid_phone("+1234567890123456")); /* 16 dígitos */
}

static void test_date(void)
{
    CHECK( validation_is_valid_date("2026-08-26"));
    CHECK( validation_is_valid_date("2024-02-29"));       /* bissexto */
    CHECK(!validation_is_valid_date("2023-02-29"));
    CHECK(!validation_is_valid_date("2026-13-01"));
    CHECK(!validation_is_valid_date("2026-00-10"));
    CHECK(!validation_is_valid_date("26-08-2026"));
    CHECK(!validation_is_valid_date(""));
    CHECK(!validation_is_valid_date(NULL));
}

static void test_time(void)
{
    CHECK( validation_is_valid_time("00:00"));
    CHECK( validation_is_valid_time("23:59"));
    CHECK(!validation_is_valid_time("24:00"));
    CHECK(!validation_is_valid_time("12:60"));
    CHECK(!validation_is_valid_time("1:00"));
    CHECK(!validation_is_valid_time(""));
}

static void test_sanitize(void)
{
    char buf[64];
    size_t n = validation_sanitize_text(buf, sizeof buf, "Olá\x01mundo");
    CHECK(n == 10); /* espaços substituem controles */
    CHECK(strcmp(buf, "Ol mundo") == 0);
}

static void test_length(void)
{
    CHECK( validation_length_between("abc",     1, 10));
    CHECK( validation_length_between("",        0, 10));
    CHECK(!validation_length_between("abcdefg", 1, 5));
    CHECK(!validation_length_between(NULL,      1, 5));
}

int main(void)
{
    test_phone();
    test_date();
    test_time();
    test_sanitize();
    test_length();
    printf("\nValidation: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
