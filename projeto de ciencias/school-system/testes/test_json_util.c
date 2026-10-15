#include "json_util.h"

#include <stdio.h>
#include <string.h>

static int passed;
static int failed;

#define CHECK(expression) do { \
    if (expression) ++passed; \
    else { ++failed; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); } \
} while (0)

int main(void)
{
    char value[128];
    const char *raw_utf8 = "{\"name\":\"Matemática, ação, João\"}";
    CHECK(json_object_get_string(raw_utf8, "name", value, sizeof value));
    CHECK(strcmp(value, "Matemática, ação, João") == 0);

    const char *escaped_unicode = "{\"name\":\"Matem\u00e1tica, Jo\u00e3o\"}";
    CHECK(json_object_get_string(escaped_unicode, "name", value, sizeof value));
    CHECK(strcmp(value, "Matemática, João") == 0);

    const char invalid_utf8[] = { '{', '\"', 'x', '\"', ':', '\"',
        (char)0xc3, '\"', '}', '\0' };
    CHECK(!json_object_get_string(invalid_utf8, "x", value, sizeof value));

    printf("JSON UTF-8: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
