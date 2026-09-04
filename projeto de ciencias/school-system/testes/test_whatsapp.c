/* test_whatsapp.c
 * Testes da camada de abstração WhatsApp.
 */
#include "whatsapp.h"
#include "validation.h"
#include "logger.h"

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
    CHECK(strlen(reply) > 0);

    CHECK(whatsapp_receive_message("12345",      "olá", reply, sizeof reply)
          == ERROR_INVALID_PHONE);

    whatsapp_shutdown();
    printf("\nWhatsApp: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
