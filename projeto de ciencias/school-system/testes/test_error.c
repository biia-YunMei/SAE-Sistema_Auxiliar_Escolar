/* test_error.c
 * Testes do módulo de erros.
 */
#include "error.h"

#include <stdio.h>
#include <string.h>

static int passed = 0, failed = 0;
#define CHECK(expr) do { \
    if (expr) { passed++; } \
    else { failed++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #expr); } \
} while (0)

int main(void)
{
    CHECK(error_string(SUCCESS) != NULL);
    CHECK(strcmp(error_string(ERROR_NOT_FOUND),       "not found")      == 0);
    CHECK(strcmp(error_string(ERROR_UNAUTHORIZED),    "unauthorized")   == 0);
    CHECK(strcmp(error_string(ERROR_DATABASE),        "database error") == 0);
    CHECK(strcmp(error_string(ERROR_INVALID_PHONE),   "invalid phone number") == 0);
    CHECK(error_string((ErrorCode)99999) != NULL);     /* fallback */
    printf("\nError: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
