/* Cliente C da API escolar de mensagens. Usa somente whoami, inbox, reply e ref. */
#include "escola_api.h"
#include "config.h"
#include "validation.h"
#include "ai.h"
#include "logger.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <time.h>
#include <stdint.h>
#if !defined(_WIN32)
#include <strings.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>
#else
#define strncasecmp _strnicmp
#include <io.h>
#include <windows.h>
#include <process.h>
#endif

#if defined(WHATSAPP_USE_CURL)
#include <curl/curl.h>
#endif

#define ESCOLA_API_BASE_URL "https://api.cidades.io/api/escola/sandbox"
#define ESCOLA_API_RESPONSE_MAX (1024U * 1024U)
#define ESCOLA_API_MESSAGE_MAX 4000U
#define ESCOLA_API_CURSOR_MAX 128U
#define ESCOLA_AUTO_POLL_SECONDS 240U
#define ESCOLA_AUTO_PAGE_SIZE 20

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
    int failed;
    char retry_after[32];
} HttpBuffer;

#if defined(WHATSAPP_USE_CURL)
static volatile int g_escola_server_curl;
static unsigned g_escola_retry_delay;
#if defined(_WIN32)
static HANDLE g_escola_worker_thread;
#else
static pthread_t g_escola_worker_thread;
#endif
static int g_escola_worker_started;
#endif

#if defined(WHATSAPP_USE_CURL)
static size_t response_write(char *data, size_t size, size_t count, void *userdata)
{
    HttpBuffer *buffer = userdata;
    if (size && count > (size_t)-1 / size) return 0;
    size_t bytes = size * count;
    if (bytes > ESCOLA_API_RESPONSE_MAX - buffer->length) {
        buffer->failed = 1;
        return 0;
    }
    size_t needed = buffer->length + bytes + 1;
    if (needed > buffer->capacity) {
        size_t next = buffer->capacity ? buffer->capacity : 4096;
        while (next < needed) {
            if (next > ESCOLA_API_RESPONSE_MAX / 2) {
                next = ESCOLA_API_RESPONSE_MAX + 1;
                break;
            }
            next *= 2;
        }
        char *grown = realloc(buffer->data, next);
        if (!grown) {
            buffer->failed = 1;
            return 0;
        }
        buffer->data = grown;
        buffer->capacity = next;
    }
    memcpy(buffer->data + buffer->length, data, bytes);
    buffer->length += bytes;
    buffer->data[buffer->length] = '\0';
    return bytes;
}

static size_t response_header(char *data, size_t size, size_t count, void *userdata)
{
    HttpBuffer *buffer = userdata;
    if (size && count > (size_t)-1 / size) return 0;
    size_t bytes = size * count;
    static const char name[] = "retry-after:";
    if (bytes > sizeof name - 1 && strncasecmp(data, name, sizeof name - 1) == 0) {
        const char *value = data + sizeof name - 1;
        size_t length = bytes - (sizeof name - 1);
        while (length && isspace((unsigned char)*value)) { ++value; --length; }
        while (length && (value[length - 1] == '\r' || value[length - 1] == '\n' ||
                          isspace((unsigned char)value[length - 1]))) --length;
        if (length >= sizeof buffer->retry_after) length = sizeof buffer->retry_after - 1;
        memcpy(buffer->retry_after, value, length);
        buffer->retry_after[length] = '\0';
    }
    return bytes;
}
#endif

static void print_safe_to(FILE *stream, const char *text)
{
    if (!text) return;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p < 0x20 || *p == 0x7f) fprintf(stream, "\\x%02x", *p);
        else fputc(*p, stream);
    }
}

static void print_safe(const char *text) { print_safe_to(stdout, text); }

#define API_BOX_WIDTH 77

static int api_unicode_ui_enabled(void)
{
    const char *choice = getenv("SAE_UI");
    if (choice && strcmp(choice, "plain") == 0) return 0;
    if (choice && (strcmp(choice, "unicode") == 0 || strcmp(choice, "nerd") == 0)) return 1;
#if defined(_WIN32)
    return _isatty(_fileno(stdout)) && GetConsoleOutputCP() == 65001;
#else
    if (!isatty(STDOUT_FILENO)) return 0;
    const char *locale = getenv("LC_ALL");
    if (!locale || !*locale) locale = getenv("LC_CTYPE");
    if (!locale || !*locale) locale = getenv("LANG");
    if (!locale || !*locale) locale = setlocale(LC_CTYPE, NULL);
    return locale && (strstr(locale, "UTF-8") || strstr(locale, "utf8") ||
                      strstr(locale, "UTF8"));
#endif
}

static size_t api_utf8_width(const char *text, size_t limit, size_t *bytes)
{
    size_t columns = 0, used = 0;
    while (text[used] && columns < limit) {
        unsigned char ch = (unsigned char)text[used];
        size_t sequence = ch < 0x80 ? 1 : (ch < 0xE0 ? 2 : (ch < 0xF0 ? 3 : 4));
        size_t i;
        for (i = 1; i < sequence && text[used + i] &&
             (((unsigned char)text[used + i] & 0xC0) == 0x80); ++i) {}
        if (i != sequence) sequence = 1;
        used += sequence;
        ++columns;
    }
    if (bytes) *bytes = used;
    return columns;
}

static void api_box_repeat(const char *piece, size_t count)
{
    while (count--) fputs(piece, stdout);
}

static void api_box_open(const char *title)
{
    if (!api_unicode_ui_enabled()) {
        printf("\n%s\n", title);
        return;
    }
    size_t title_width = api_utf8_width(title, API_BOX_WIDTH - 2, NULL);
    size_t remaining = API_BOX_WIDTH - title_width - 2;
    fputs("╭", stdout);
    api_box_repeat("─", remaining / 2);
    printf(" %s ", title);
    api_box_repeat("─", remaining - remaining / 2);
    puts("╮");
}

static void api_box_line(const char *text)
{
    if (!api_unicode_ui_enabled()) {
        fputs("  ", stdout); print_safe(text); putchar('\n');
        return;
    }
    size_t bytes = 0;
    size_t width = api_utf8_width(text, API_BOX_WIDTH - 2, &bytes);
    char clipped[2048];
    if (bytes >= sizeof clipped) bytes = sizeof clipped - 1;
    memcpy(clipped, text, bytes);
    clipped[bytes] = '\0';
    fputs("│  ", stdout);
    print_safe_to(stdout, clipped);
    if (width < API_BOX_WIDTH - 2) api_box_repeat(" ", API_BOX_WIDTH - 2 - width);
    puts("│");
}

static void api_error_box(const char *message)
{
    if (!api_unicode_ui_enabled()) {
        fputs("ERRO: ", stderr);
        print_safe_to(stderr, message);
        fputc('\n', stderr);
        return;
    }

    fputs("╭", stderr);
    for (size_t i = 0; i < (API_BOX_WIDTH - 6) / 2; ++i) fputs("─", stderr);
    fputs(" ERRO ", stderr);
    for (size_t i = 0; i < API_BOX_WIDTH - 6 - (API_BOX_WIDTH - 6) / 2; ++i)
        fputs("─", stderr);
    fputs("╮\n", stderr);

    const char *p = message ? message : "Ocorreu um erro.";
    do {
        size_t bytes = 0;
        (void)api_utf8_width(p, API_BOX_WIDTH - 2, &bytes);
        if (!bytes && *p) bytes = 1;
        if (p[bytes] && p[bytes] != ' ') {
            size_t split = bytes;
            while (split && p[split] != ' ') --split;
            if (split) bytes = split;
        }
        char line[API_BOX_WIDTH * 4];
        size_t copied = bytes;
        if (copied >= sizeof line) copied = sizeof line - 1;
        memcpy(line, p, copied);
        line[copied] = '\0';
        size_t width = api_utf8_width(line, API_BOX_WIDTH - 2, NULL);
        fputs("│  ", stderr);
        print_safe_to(stderr, line);
        for (size_t i = width; i < API_BOX_WIDTH - 2; ++i) fputc(' ', stderr);
        fputs("│\n", stderr);
        p += bytes;
        while (*p == ' ') ++p;
    } while (*p);

    fputs("╰", stderr);
    for (size_t i = 0; i < API_BOX_WIDTH; ++i) fputs("─", stderr);
    fputs("╯\n", stderr);
}

static void api_box_wrapped(const char *label, const char *text)
{
    const char *p = text ? text : "";
    int first = 1;
    do {
        const char *prefix = first ? label : "  ";
        size_t prefix_width = api_utf8_width(prefix, API_BOX_WIDTH - 2, NULL);
        size_t max_width = API_BOX_WIDTH - 2 - prefix_width;
        if (*p == '\n' || *p == '\r') {
            api_box_line("  ");
            if (*p == '\r') ++p;
            if (*p == '\n') ++p;
            first = 0;
            continue;
        }
        size_t bytes = 0;
        (void)api_utf8_width(p, max_width, &bytes);
        if (!bytes && *p) bytes = 1;
        size_t newline = 0;
        while (newline < bytes && p[newline] != '\n' && p[newline] != '\r') ++newline;
        if (newline < bytes) {
            bytes = newline;
        } else if (p[bytes]) {
            size_t break_at = bytes;
            while (break_at && p[break_at] != ' ') --break_at;
            if (break_at) bytes = break_at;
        }
        char line[2048];
        size_t prefix_bytes = strlen(prefix);
        if (prefix_bytes + bytes >= sizeof line) bytes = sizeof line - prefix_bytes - 1;
        memcpy(line, prefix, prefix_bytes);
        memcpy(line + prefix_bytes, p, bytes);
        line[prefix_bytes + bytes] = '\0';
        api_box_line(line);
        p += bytes;
        if (*p == '\r') ++p;
        if (*p == '\n') ++p;
        else while (*p == ' ') ++p;
        first = 0;
    } while (*p);
}

static void api_box_close(void)
{
    if (!api_unicode_ui_enabled()) { putchar('\n'); return; }
    fputs("╰", stdout);
    api_box_repeat("─", API_BOX_WIDTH);
    puts("╯");
}

static void api_print_json_pretty(const char *json, size_t length)
{
    if (api_unicode_ui_enabled()) api_box_open("RESPOSTA DA API ESCOLAR");
    else puts("Resposta da API escolar:");
    unsigned depth = 0;
    int quoted = 0, escaped = 0, line_has_text = 0;
    char line[2048];
    size_t used = 0;
    for (size_t i = 0; i < length; ++i) {
        char ch = json[i];
        if (quoted) {
            if (used + 1 < sizeof line) line[used++] = ch;
            if (escaped) escaped = 0;
            else if (ch == '\\') escaped = 1;
            else if (ch == '"') quoted = 0;
            line_has_text = 1;
            continue;
        }
        if (ch == '"') {
            quoted = 1;
            if (used + 1 < sizeof line) line[used++] = ch;
            line_has_text = 1;
        } else if (ch == '{' || ch == '[') {
            if (used + 1 < sizeof line) line[used++] = ch;
            line[used] = '\0';
            api_box_line(line);
            used = 0;
            ++depth;
            line_has_text = 0;
            for (unsigned j = 0; j < depth * 2 && used + 1 < sizeof line; ++j)
                line[used++] = ' ';
        } else if (ch == '}' || ch == ']') {
            if (line_has_text && used) {
                line[used] = '\0'; api_box_line(line);
                used = 0; line_has_text = 0;
            }
            used = 0;
            if (depth) --depth;
            for (unsigned j = 0; j < depth * 2 && used + 1 < sizeof line; ++j)
                line[used++] = ' ';
            if (used + 1 < sizeof line) line[used++] = ch;
            line[used] = '\0'; api_box_line(line);
            used = 0;
        } else if (ch == ',') {
            if (used + 1 < sizeof line) line[used++] = ch;
            line[used] = '\0'; api_box_line(line);
            used = 0; line_has_text = 0;
            for (unsigned j = 0; j < depth * 2 && used + 1 < sizeof line; ++j)
                line[used++] = ' ';
        } else if (ch == ':') {
            if (used + 2 < sizeof line) { line[used++] = ':'; line[used++] = ' '; }
            line_has_text = 1;
        } else if (!isspace((unsigned char)ch)) {
            if (used + 1 < sizeof line) line[used++] = ch;
            line_has_text = 1;
        }
    }
    if (used) { line[used] = '\0'; api_box_line(line); }
    if (api_unicode_ui_enabled()) api_box_close();
}

static int key_is_safe(const char *key)
{
    if (!key || strncmp(key, "esc_", 4) != 0 || strlen(key) < 8) return 0;
    for (const unsigned char *p = (const unsigned char *)key; *p; ++p)
        if (!(isalnum(*p) || *p == '_' || *p == '-' || *p == '.')) return 0;
    return 1;
}

static int json_skip_string(const char **cursor)
{
    const char *p = *cursor;
    if (*p != '"') return 0;
    ++p;
    while (*p) {
        if (*p == '"') { *cursor = p + 1; return 1; }
        if (*p == '\\') {
            ++p;
            if (!*p) return 0;
            if (*p == 'u') {
                for (int i = 0; i < 4; ++i) {
                    if (!p[1 + i] || !isxdigit((unsigned char)p[1 + i])) return 0;
                }
                p += 4;
            } else if (!strchr("\"\\/bfnrt", *p)) {
                return 0;
            }
        } else if ((unsigned char)*p < 0x20) return 0;
        ++p;
    }
    return 0;
}

static const char *json_skip_space(const char *p)
{
    while (*p && isspace((unsigned char)*p)) ++p;
    return p;
}

static int json_skip_value(const char **cursor, unsigned depth)
{
    if (depth > 32) return 0;
    const char *p = json_skip_space(*cursor);
    if (*p == '"') {
        if (!json_skip_string(&p)) return 0;
        *cursor = p;
        return 1;
    }
    if (*p == '{' || *p == '[') {
        char close = *p == '{' ? '}' : ']';
        int object = *p == '{';
        ++p;
        p = json_skip_space(p);
        if (*p == close) { *cursor = p + 1; return 1; }
        for (;;) {
            if (object) {
                if (*p != '"' || !json_skip_string(&p)) return 0;
                p = json_skip_space(p);
                if (*p++ != ':') return 0;
            }
            if (!json_skip_value(&p, depth + 1)) return 0;
            p = json_skip_space(p);
            if (*p == close) { *cursor = p + 1; return 1; }
            if (*p++ != ',') return 0;
            p = json_skip_space(p);
        }
    }
    const char *start = p;
    while (*p && *p != ',' && *p != ']' && *p != '}' && !isspace((unsigned char)*p)) ++p;
    if (p == start) return 0;
    *cursor = p;
    return 1;
}

static int json_string_decode(const char *start, size_t length, char *out, size_t cap)
{
    if (!start || length < 2 || start[0] != '"' || start[length - 1] != '"' || cap == 0)
        return 0;
    size_t used = 0;
    const char *p = start + 1, *end = start + length - 1;
    while (p < end) {
        unsigned int codepoint;
        if (*p != '\\') {
            if (used + 1 >= cap) return 0;
            out[used++] = *p++;
            continue;
        } else {
            ++p;
            if (p >= end) return 0;
            char escaped = *p++;
            switch (escaped) {
            case '"': codepoint = '"'; break;
            case '\\': codepoint = '\\'; break;
            case '/': codepoint = '/'; break;
            case 'b': codepoint = '\b'; break;
            case 'f': codepoint = '\f'; break;
            case 'n': codepoint = '\n'; break;
            case 'r': codepoint = '\r'; break;
            case 't': codepoint = '\t'; break;
            case 'u': {
                if (end - p < 4) return 0;
                codepoint = 0;
                for (int i = 0; i < 4; ++i) {
                    unsigned char c = (unsigned char)*p++;
                    codepoint <<= 4;
                    if (c >= '0' && c <= '9') codepoint |= (unsigned)(c - '0');
                    else if (tolower(c) >= 'a' && tolower(c) <= 'f') codepoint |= (unsigned)(tolower(c) - 'a' + 10);
                    else return 0;
                }
                if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                    if (end - p < 6 || p[0] != '\\' || p[1] != 'u') return 0;
                    p += 2;
                    unsigned int low = 0;
                    for (int i = 0; i < 4; ++i) {
                        unsigned char c = (unsigned char)*p++;
                        low <<= 4;
                        if (c >= '0' && c <= '9') low |= (unsigned)(c - '0');
                        else if (tolower(c) >= 'a' && tolower(c) <= 'f') low |= (unsigned)(tolower(c) - 'a' + 10);
                        else return 0;
                    }
                    if (low < 0xdc00 || low > 0xdfff) return 0;
                    codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
                } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) return 0;
                break;
            }
            default: return 0;
            }
        }
        unsigned char bytes[4];
        size_t count;
        if (codepoint <= 0x7f) { bytes[0] = (unsigned char)codepoint; count = 1; }
        else if (codepoint <= 0x7ff) {
            bytes[0] = (unsigned char)(0xc0 | (codepoint >> 6));
            bytes[1] = (unsigned char)(0x80 | (codepoint & 0x3f)); count = 2;
        } else if (codepoint <= 0xffff) {
            bytes[0] = (unsigned char)(0xe0 | (codepoint >> 12));
            bytes[1] = (unsigned char)(0x80 | ((codepoint >> 6) & 0x3f));
            bytes[2] = (unsigned char)(0x80 | (codepoint & 0x3f)); count = 3;
        } else {
            bytes[0] = (unsigned char)(0xf0 | (codepoint >> 18));
            bytes[1] = (unsigned char)(0x80 | ((codepoint >> 12) & 0x3f));
            bytes[2] = (unsigned char)(0x80 | ((codepoint >> 6) & 0x3f));
            bytes[3] = (unsigned char)(0x80 | (codepoint & 0x3f)); count = 4;
        }
        if (used + count >= cap) return 0;
        memcpy(out + used, bytes, count);
        used += count;
    }
    out[used] = '\0';
    return 1;
}

static int json_object_member(const char *object, size_t length, const char *name,
                              const char **value, size_t *value_length)
{
    const char *p = json_skip_space(object);
    const char *end = object + length;
    if (p >= end || *p++ != '{') return 0;
    p = json_skip_space(p);
    while (p < end && *p != '}') {
        const char *key_start = p;
        if (*p != '"' || !json_skip_string(&p)) return 0;
        char key[96];
        if (!json_string_decode(key_start, (size_t)(p - key_start), key, sizeof key)) return 0;
        p = json_skip_space(p);
        if (p >= end || *p++ != ':') return 0;
        p = json_skip_space(p);
        const char *vstart = p;
        if (!json_skip_value(&p, 0) || p > end) return 0;
        if (strcmp(key, name) == 0) {
            *value = vstart;
            *value_length = (size_t)(p - vstart);
            return 1;
        }
        p = json_skip_space(p);
        if (p < end && *p == ',') { ++p; p = json_skip_space(p); }
        else if (p < end && *p != '}') return 0;
    }
    return 0;
}

static int json_get_string(const char *object, size_t length, const char *name,
                           char *out, size_t cap)
{
    const char *value;
    size_t value_length;
    if (!json_object_member(object, length, name, &value, &value_length) ||
        !value_length || *value != '"') return 0;
    return json_string_decode(value, value_length, out, cap);
}

static int json_value_is_true(const char *object, size_t length, const char *name)
{
    const char *value;
    size_t value_length;
    return json_object_member(object, length, name, &value, &value_length) &&
           value_length == 4 && memcmp(value, "true", 4) == 0;
}

static int json_get_object(const char *object, size_t length, const char *name,
                           const char **out, size_t *out_length)
{
    const char *value;
    size_t value_length;
    if (!json_object_member(object, length, name, &value, &value_length) ||
        !value_length || *value != '{') return 0;
    *out = value;
    *out_length = value_length;
    return 1;
}

static int valid_cursor(const char *cursor)
{
    size_t length = cursor ? strlen(cursor) : 0;
    if (!length || length > ESCOLA_API_CURSOR_MAX) return 0;
    for (const unsigned char *p = (const unsigned char *)cursor; *p; ++p)
        if (!(isalnum(*p) || *p == '_' || *p == '-')) return 0;
    return 1;
}

static int read_cursor(const char *path, char cursor[ESCOLA_API_CURSOR_MAX + 1])
{
    FILE *file = fopen(path, "rb");
    if (!file) return errno == ENOENT ? 0 : -1;
    char line[ESCOLA_API_CURSOR_MAX + 3];
    if (!fgets(line, (int)sizeof line, file)) {
        int failed = ferror(file);
        fclose(file);
        return failed ? -1 : 0;
    }
    int trailing = fgetc(file);
    int failed = ferror(file);
    fclose(file);
    if (trailing != EOF || failed) return -1;
    line[strcspn(line, "\r\n")] = '\0';
    if (strlen(line) > ESCOLA_API_CURSOR_MAX) return -1;
    memcpy(cursor, line, strlen(line) + 1);
    return valid_cursor(cursor) ? 1 : -1;
}

static int save_cursor(const char *path, const char *cursor)
{
    char temporary[600];
    int n = snprintf(temporary, sizeof temporary, "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof temporary) return 0;
    FILE *file = fopen(temporary, "wb");
    if (!file) return 0;
#if !defined(_WIN32)
    (void)chmod(temporary, 0600);
#endif
    int ok = fprintf(file, "%s\n", cursor) > 0 && fflush(file) == 0;
    if (fclose(file) != 0) ok = 0;
#if defined(_WIN32)
    if (ok) (void)remove(path);
#endif
    if (!ok || rename(temporary, path) != 0) {
        (void)remove(temporary);
        return 0;
    }
    return 1;
}

#if defined(WHATSAPP_USE_CURL)
static int escola_api_request(const AppConfig *cfg, const char *method,
                             const char *path, const char *request_body,
                             HttpBuffer *response, long *http_status)
{
    int owns_curl_runtime = !g_escola_server_curl;
    if (owns_curl_runtime && curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        return ERROR_NETWORK;
    CURL *curl = curl_easy_init();
    if (!curl) {
        if (owns_curl_runtime) curl_global_cleanup();
        return ERROR_NETWORK;
    }
    char url[512], authorization[320];
    int n = snprintf(url, sizeof url, "%s%s", ESCOLA_API_BASE_URL, path);
    int auth_n = snprintf(authorization, sizeof authorization, "X-Escola-Key: %s", cfg->escola_api_key);
    if (n < 0 || (size_t)n >= sizeof url || auth_n < 0 || (size_t)auth_n >= sizeof authorization) {
        curl_easy_cleanup(curl);
        if (owns_curl_runtime) curl_global_cleanup();
        return ERROR_TOO_LONG;
    }
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, authorization);
    if (request_body) headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!headers) {
        curl_easy_cleanup(curl);
        if (owns_curl_runtime) curl_global_cleanup();
        return ERROR_INTERNAL;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, response_header);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, response);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 25L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(request_body));
    }
    CURLcode result = curl_easy_perform(curl);
    if (result == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, http_status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (owns_curl_runtime) curl_global_cleanup();
    if (response->failed) return ERROR_TOO_LONG;
    return result == CURLE_OK ? SUCCESS : ERROR_NETWORK;
}
#endif

static int load_config(AppConfig *cfg)
{
    const AppConfig *current = config_current();
    if (current) {
        *cfg = *current;
    } else if (config_load(cfg) != SUCCESS) {
        api_error_box("Não foi possível carregar a configuração do SAE.");
        return 0;
    }
    if (!key_is_safe(cfg->escola_api_key)) {
        api_error_box("Configure ESCOLA_API_KEY no .env local ou no ambiente. Não envie a chave no comando.");
        return 0;
    }
    return 1;
}

static int perform(const char *path, const char *method, const char *body,
                   HttpBuffer *response)
{
    AppConfig cfg;
    if (!load_config(&cfg)) return ERROR_CONFIG;
#if !defined(WHATSAPP_USE_CURL)
    (void)path; (void)method; (void)body; (void)response;
    api_error_box("Este build não tem libcurl; instale libcurl e recompile o SAE.");
    return ERROR_NOT_IMPLEMENTED;
#else
    long status = 0;
    int rc = escola_api_request(&cfg, method, path, body, response, &status);
    if (rc != SUCCESS) {
        char message[128];
        snprintf(message, sizeof message, "Falha de rede ao consultar a API da escola (código %d).", rc);
        api_error_box(message);
        return rc;
    }
    if (status < 200 || status >= 300) {
        char error[128] = {0}, message[512] = {0};
        if (!json_get_string(response->data, response->length, "erro", error, sizeof error))
            snprintf(error, sizeof error, "http_%ld", status);
        (void)json_get_string(response->data, response->length, "mensagem", message, sizeof message);
        char detail[768];
        snprintf(detail, sizeof detail, "API da escola: %s%s%s", error,
                 message[0] ? " — " : "", message);
        if (status == 429 && response->retry_after[0]) {
            size_t used = strlen(detail);
            snprintf(detail + used, sizeof detail - used,
                     " (aguarde %s segundos, conforme Retry-After)", response->retry_after);
        }
        api_error_box(detail);
        if (strcmp(error, "chave_invalida") == 0)
            api_error_box("Pare aqui e solicite uma nova chave ao responsável.");
        return status == 429 ? ERROR_FORBIDDEN : ERROR_HTTP;
    }
    return SUCCESS;
#endif
}

static int print_raw_response(const char *path)
{
    HttpBuffer response = {0};
    int rc = perform(path, "GET", NULL, &response);
    if (rc == SUCCESS) {
        if (!response.data || response.length == 0) puts("(resposta vazia)");
        else api_print_json_pretty(response.data, response.length);
    }
    free(response.data);
    return rc;
}

int escola_api_whoami(void)
{
    return print_raw_response("/whoami");
}

int escola_api_ref(void)
{
    return print_raw_response("/ref");
}

static int valid_message_id(const char *id)
{
    if (!id || strncmp(id, "m_", 2) != 0 || !id[2]) return 0;
    for (const unsigned char *p = (const unsigned char *)id + 2; *p; ++p)
        if (!isdigit(*p)) return 0;
    return strlen(id) <= 40;
}

static int json_quote(const char *input, char *out, size_t cap)
{
    size_t used = 0;
    if (!input || cap < 3) return 0;
    out[used++] = '"';
    for (const unsigned char *p = (const unsigned char *)input; *p; ++p) {
        const char *escaped = NULL;
        char unicode[7], one[2] = {(char)*p, '\0'};
        switch (*p) {
        case '"': escaped = "\\\""; break;
        case '\\': escaped = "\\\\"; break;
        case '\b': escaped = "\\b"; break;
        case '\f': escaped = "\\f"; break;
        case '\n': escaped = "\\n"; break;
        case '\r': escaped = "\\r"; break;
        case '\t': escaped = "\\t"; break;
        default:
            if (*p < 0x20) { (void)snprintf(unicode, sizeof unicode, "\\u%04x", *p); escaped = unicode; }
        }
        const char *part = escaped ? escaped : one;
        size_t length = strlen(part);
        if (used + length + 2 > cap) return 0;
        memcpy(out + used, part, length); used += length;
    }
    out[used++] = '"'; out[used] = '\0';
    return 1;
}

int escola_api_reply(const char *message_id, const char *text)
{
    if (!valid_message_id(message_id) || !text ||
        !validation_length_between(text, 1, (int)ESCOLA_API_MESSAGE_MAX)) {
        char error[160];
        snprintf(error, sizeof error,
                 "Use um ID m_... recebido pela inbox e uma resposta de até %u caracteres.",
                 (unsigned)ESCOLA_API_MESSAGE_MAX);
        api_error_box(error);
        return ERROR_INVALID_INPUT;
    }
    char quoted_id[64], quoted_text[ESCOLA_API_MESSAGE_MAX * 6 + 3];
    char body[ESCOLA_API_MESSAGE_MAX * 6 + 128];
    if (!json_quote(message_id, quoted_id, sizeof quoted_id) ||
        !json_quote(text, quoted_text, sizeof quoted_text)) return ERROR_TOO_LONG;
    int n = snprintf(body, sizeof body, "{\"in_reply_to\":%s,\"text\":%s}", quoted_id, quoted_text);
    if (n < 0 || (size_t)n >= sizeof body) return ERROR_TOO_LONG;
    HttpBuffer response = {0};
    int rc = perform("/reply", "POST", body, &response);
    if (rc == SUCCESS) {
        char sent_at[64] = {0};
        if (response.data)
            (void)json_get_string(response.data, response.length, "enviado_em",
                                  sent_at, sizeof sent_at);
        api_box_open("RESPOSTA ACEITA PELA API");
        char line[128];
        snprintf(line, sizeof line, "Mensagem de origem: %s", message_id);
        api_box_line(line);
        api_box_wrapped("Resposta: ", text);
        if (sent_at[0]) {
            snprintf(line, sizeof line, "Aceita em: %s", sent_at);
            api_box_line(line);
        }
        api_box_line("Entrega ao WhatsApp: não confirmada pela API.");
        api_box_close();
    }
    free(response.data);
    return rc;
}

static int print_message(const char *object, size_t length)
{
    char id[64] = {0}, from_ref[80] = {0}, kind[32] = {0};
    char moderation[32] = {0}, timestamp[64] = {0}, text[ESCOLA_API_MESSAGE_MAX + 1] = {0};
    if (!json_get_string(object, length, "moderacao", moderation, sizeof moderation) ||
        strcmp(moderation, "ok") != 0) return 0;
    if (!json_get_string(object, length, "id", id, sizeof id) ||
        !json_get_string(object, length, "kind", kind, sizeof kind)) return -1;
    (void)json_get_string(object, length, "from_ref", from_ref, sizeof from_ref);
    (void)json_get_string(object, length, "ts", timestamp, sizeof timestamp);
    char title[112], line[768];
    snprintf(title, sizeof title, "MENSAGEM %s", id);
    api_box_open(title);
    snprintf(line, sizeof line, "Tipo: %s", kind);
    api_box_line(line);
    if (from_ref[0]) {
        snprintf(line, sizeof line, "Remetente: %s", from_ref);
        api_box_line(line);
    }
    if (timestamp[0]) {
        snprintf(line, sizeof line, "Recebida em: %s", timestamp);
        api_box_line(line);
    }
    if (json_get_string(object, length, "text", text, sizeof text)) {
        api_box_wrapped(json_value_is_true(object, length, "transcrito")
                            ? "Áudio transcrito: " : "Mensagem: ", text);
    }
    const char *image;
    size_t image_length;
    if (json_get_object(object, length, "imagem", &image, &image_length)) {
        char subject[96] = {0}, description[512] = {0}, sheet[80] = {0};
        (void)json_get_string(image, image_length, "materia", subject, sizeof subject);
        (void)json_get_string(image, image_length, "descricao", description, sizeof description);
        (void)json_get_string(image, image_length, "codigo_folha", sheet, sizeof sheet);
        snprintf(line, sizeof line, "Imagem: %s%s%s%s%s%s",
                 subject, subject[0] ? " — " : "", description,
                 sheet[0] ? " [folha " : "", sheet,
                 sheet[0] ? "]" : "");
        api_box_wrapped("Imagem: ", line + strlen("Imagem: "));
    }
    api_box_close();
    return 1;
}

static int array_bounds(const char *json, size_t length, const char *name,
                        const char **begin, const char **end)
{
    const char *value;
    size_t value_length;
    if (!json_object_member(json, length, name, &value, &value_length) ||
        !value_length || *value != '[') return 0;
    *begin = value + 1;
    *end = value + value_length - 1;
    return 1;
}

static int print_inbox(const char *json, size_t length, char cursor_out[ESCOLA_API_CURSOR_MAX + 1])
{
    if (!json_get_string(json, length, "proximo_cursor", cursor_out,
                         ESCOLA_API_CURSOR_MAX + 1) || !valid_cursor(cursor_out)) return 0;
    const char *p, *end;
    if (!array_bounds(json, length, "mensagens", &p, &end)) return 0;
    size_t shown = 0, hidden = 0;
    p = json_skip_space(p);
    while (p < end && *p != ']') {
        const char *start = p;
        if (*p != '{' || !json_skip_value(&p, 0) || p > end) return 0;
        int result = print_message(start, (size_t)(p - start));
        if (result < 0) return 0;
        if (result == 0) ++hidden; else ++shown;
        p = json_skip_space(p);
        if (p < end && *p == ',') { ++p; p = json_skip_space(p); }
        else if (p < end && *p != ']') return 0;
    }
    if (!shown && !hidden) puts("Nenhuma mensagem nova.");
    if (hidden) printf("%zu mensagem(ns) omitida(s) por moderação ou formato incompleto.\n", hidden);
    return 1;
}

int escola_api_inbox(int limit)
{
    if (limit < 1 || limit > 50) {
        fprintf(stderr, "O limite deve estar entre 1 e 50.\n");
        return ERROR_INVALID_INPUT;
    }
    AppConfig cfg;
    if (!load_config(&cfg)) return ERROR_CONFIG;
    char cursor[ESCOLA_API_CURSOR_MAX + 1] = {0};
    int cursor_result = read_cursor(cfg.escola_cursor_path, cursor);
    if (cursor_result < 0) {
        fprintf(stderr, "Cursor inválido ou ilegível em %s; não vou avançar a inbox.\n",
                cfg.escola_cursor_path);
        return ERROR_CONFIG;
    }
    char path[256];
    int n;
    if (cursor_result > 0)
        n = snprintf(path, sizeof path, "/inbox?limit=%d&since=%s", limit, cursor);
    else
        n = snprintf(path, sizeof path, "/inbox?limit=%d", limit);
    if (n < 0 || (size_t)n >= sizeof path) return ERROR_TOO_LONG;

    HttpBuffer response = {0};
    int rc = perform(path, "GET", NULL, &response);
    if (rc != SUCCESS) { free(response.data); return rc; }
    char next_cursor[ESCOLA_API_CURSOR_MAX + 1] = {0};
    if (!response.data || !print_inbox(response.data, response.length, next_cursor)) {
        fprintf(stderr, "A API retornou uma inbox malformada; o cursor anterior foi preservado.\n");
        free(response.data);
        return ERROR_HTTP;
    }
    free(response.data);
    if (!save_cursor(cfg.escola_cursor_path, next_cursor)) {
        fprintf(stderr, "Mensagens exibidas, mas não consegui persistir o cursor em %s.\n",
                cfg.escola_cursor_path);
        return ERROR_INTERNAL;
    }
    return SUCCESS;
}

#if defined(WHATSAPP_USE_CURL)
static int worker_fetch_inbox(const AppConfig *cfg, const char *cursor,
                              HttpBuffer *response,
                              char next_cursor[ESCOLA_API_CURSOR_MAX + 1])
{
    char path[256];
    int n = cursor && *cursor
        ? snprintf(path, sizeof path, "/inbox?limit=%d&since=%s", ESCOLA_AUTO_PAGE_SIZE, cursor)
        : snprintf(path, sizeof path, "/inbox?limit=%d", ESCOLA_AUTO_PAGE_SIZE);
    if (n < 0 || (size_t)n >= sizeof path) return ERROR_TOO_LONG;
    long status = 0;
    int rc = escola_api_request(cfg, "GET", path, NULL, response, &status);
    if (rc != SUCCESS) return rc;
    if (status < 200 || status >= 300) {
        char error[128] = "erro_http";
        if (response->data)
            (void)json_get_string(response->data, response->length, "erro", error, sizeof error);
        if (status == 429 && response->retry_after[0]) {
            char *end = NULL;
            unsigned long seconds = strtoul(response->retry_after, &end, 10);
            if (end && !*end && seconds > 0 && seconds <= 86400)
                g_escola_retry_delay = (unsigned)seconds;
        }
        log_warn("escola: consulta automática da inbox recusada (%s, HTTP %ld)", error, status);
        if (strcmp(error, "chave_invalida") == 0) return ERROR_CONFIG;
        if (strcmp(error, "rota_nao_liberada") == 0) return ERROR_NOT_IMPLEMENTED;
        return status == 429 ? ERROR_FORBIDDEN : ERROR_HTTP;
    }
    if (!response->data ||
        !json_get_string(response->data, response->length, "proximo_cursor",
                         next_cursor, ESCOLA_API_CURSOR_MAX + 1) ||
        !valid_cursor(next_cursor)) return ERROR_HTTP;
    return SUCCESS;
}

static int worker_message_count(const char *json, size_t length, const char **begin,
                                const char **end, size_t *count)
{
    if (!array_bounds(json, length, "mensagens", begin, end)) return 0;
    *count = 0;
    const char *p = json_skip_space(*begin);
    while (p < *end && *p != ']') {
        if (*p != '{' || !json_skip_value(&p, 0) || p > *end) return 0;
        ++*count;
        p = json_skip_space(p);
        if (p < *end && *p == ',') { ++p; p = json_skip_space(p); }
        else if (p < *end && *p != ']') return 0;
    }
    return 1;
}

static int worker_ledger_path(const AppConfig *cfg, char path[600])
{
    int n = snprintf(path, 600, "%s.done", cfg->escola_cursor_path);
    return n >= 0 && n < 600;
}

static int worker_bootstrap_path(const AppConfig *cfg, char path[600])
{
    int n = snprintf(path, 600, "%s.bootstrap", cfg->escola_cursor_path);
    return n >= 0 && n < 600;
}

static int worker_message_done(const char *path, const char *id)
{
    FILE *file = fopen(path, "rb");
    if (!file) return errno == ENOENT ? 0 : -1;
    char line[64];
    while (fgets(line, sizeof line, file)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, id) == 0) { fclose(file); return 1; }
    }
    int failed = ferror(file);
    fclose(file);
    return failed ? -1 : 0;
}

static int worker_mark_done(const char *path, const char *id)
{
    FILE *file = fopen(path, "ab");
    if (!file) return 0;
#if !defined(_WIN32)
    (void)chmod(path, 0600);
#endif
    int ok = fprintf(file, "%s\n", id) > 0 && fflush(file) == 0;
#if !defined(_WIN32)
    if (ok && fsync(fileno(file)) != 0) ok = 0;
#else
    if (ok && _commit(_fileno(file)) != 0) ok = 0;
#endif
    if (fclose(file) != 0) ok = 0;
    return ok;
}

static int worker_opt_out_command(const char *text)
{
    while (isspace((unsigned char)*text)) ++text;
    size_t length = strlen(text);
    while (length && isspace((unsigned char)text[length - 1])) --length;
    static const char *commands[] = { "PARAR", "SAIR", "VOLTAR" };
    for (size_t i = 0; i < sizeof commands / sizeof commands[0]; ++i) {
        if (strlen(commands[i]) != length) continue;
        size_t j = 0;
        while (j < length && toupper((unsigned char)text[j]) == commands[i][j]) ++j;
        if (j == length) return 1;
    }
    return 0;
}

static int worker_send_reply(const AppConfig *cfg, const char *id, const char *text)
{
    char quoted_id[64], quoted_text[ESCOLA_API_MESSAGE_MAX * 6 + 3];
    char body[ESCOLA_API_MESSAGE_MAX * 6 + 128];
    if (!json_quote(id, quoted_id, sizeof quoted_id) ||
        !json_quote(text, quoted_text, sizeof quoted_text)) return ERROR_TOO_LONG;
    int n = snprintf(body, sizeof body, "{\"in_reply_to\":%s,\"text\":%s}",
                     quoted_id, quoted_text);
    if (n < 0 || (size_t)n >= sizeof body) return ERROR_TOO_LONG;
    HttpBuffer response = {0};
    long status = 0;
    int rc = escola_api_request(cfg, "POST", "/reply", body, &response, &status);
    if (rc == SUCCESS && (status < 200 || status >= 300)) {
        char error[128] = "erro_http";
        if (response.data)
            (void)json_get_string(response.data, response.length, "erro", error, sizeof error);
        if (status == 429 && response.retry_after[0]) {
            char *end = NULL;
            unsigned long seconds = strtoul(response.retry_after, &end, 10);
            if (end && !*end && seconds > 0 && seconds <= 86400)
                g_escola_retry_delay = (unsigned)seconds;
        }
        log_warn("escola: resposta automática para %s recusada (%s, HTTP %ld)", id, error, status);
        if (strcmp(error, "chave_invalida") == 0) rc = ERROR_CONFIG;
        else if (strcmp(error, "rota_nao_liberada") == 0) rc = ERROR_NOT_IMPLEMENTED;
        else if (strcmp(error, "fora_da_allowlist") == 0 ||
                 strcmp(error, "fora_da_janela_24h") == 0 ||
                 strcmp(error, "in_reply_to_invalido") == 0) rc = ERROR_NOT_FOUND;
        else rc = status == 429 ? ERROR_FORBIDDEN : ERROR_HTTP;
    }
    free(response.data);
    return rc;
}

static int worker_handle_message(const AppConfig *cfg, const char *ledger,
                                const char *object, size_t length)
{
    char id[64] = {0}, moderation[32] = {0}, kind[32] = {0};
    char text[ESCOLA_API_MESSAGE_MAX + 1] = {0};
    if (!json_get_string(object, length, "id", id, sizeof id) ||
        !valid_message_id(id)) return ERROR_HTTP;
    int done = worker_message_done(ledger, id);
    if (done < 0) return ERROR_INTERNAL;
    if (done) return SUCCESS;

    if (!json_get_string(object, length, "moderacao", moderation, sizeof moderation) ||
        strcmp(moderation, "ok") != 0 ||
        !json_get_string(object, length, "kind", kind, sizeof kind))
        return worker_mark_done(ledger, id) ? SUCCESS : ERROR_INTERNAL;

    int has_text = json_get_string(object, length, "text", text, sizeof text);
    if (!has_text && strcmp(kind, "image") == 0) {
        const char *image = NULL;
        size_t image_length = 0;
        char description[512] = {0}, subject[80] = {0};
        if (json_get_object(object, length, "imagem", &image, &image_length)) {
            (void)json_get_string(image, image_length, "descricao", description, sizeof description);
            (void)json_get_string(image, image_length, "materia", subject, sizeof subject);
        }
        if (description[0]) {
            int n = snprintf(text, sizeof text,
                "Um aluno enviou uma imagem de exercício. Matéria: %s. Descrição: %s. "
                "Ajude com uma pista e uma pergunta, sem entregar a resposta pronta.",
                subject[0] ? subject : "não informada", description);
            has_text = n > 0 && (size_t)n < sizeof text;
        }
    }
    if (!has_text || !text[0] || worker_opt_out_command(text))
        return worker_mark_done(ledger, id) ? SUCCESS : ERROR_INTERNAL;

    char answer[4001];
    int rc;
    if (strlen(text) > 800) {
        snprintf(answer, sizeof answer,
                 "Sua mensagem ficou longa demais para eu analisar de uma vez. "
                 "Pode dividi-la em partes menores?");
        rc = SUCCESS;
    } else {
        rc = ai_answer_student_question(text, "", answer, sizeof answer);
    }
    if (rc != SUCCESS || !answer[0]) {
        if (rc == ERROR_AI)
            log_warn("escola: IA não preparou resposta para %s; mensagem segue pendente", id);
        else
            log_warn("escola: não foi possível preparar resposta para %s (erro interno %d)", id, rc);
        return rc == SUCCESS ? ERROR_AI : rc;
    }
    rc = worker_send_reply(cfg, id, answer);
    if (rc == ERROR_NOT_FOUND) {
        log_info("escola: mensagem %s não pode receber resposta pela API", id);
        return worker_mark_done(ledger, id) ? SUCCESS : ERROR_INTERNAL;
    }
    if (rc != SUCCESS) return rc;
    if (!worker_mark_done(ledger, id)) return ERROR_INTERNAL;
    log_info("escola: resposta automática aceita para mensagem %s", id);
    return SUCCESS;
}

static void worker_wait(volatile int *should_stop, unsigned seconds)
{
    for (unsigned i = 0; i < seconds && !*should_stop; ++i) {
#if defined(_WIN32)
        Sleep(1000);
#else
        (void)sleep(1);
#endif
    }
}

static int worker_bootstrap_cursor(const AppConfig *cfg, volatile int *should_stop)
{
    char cursor[ESCOLA_API_CURSOR_MAX + 1] = {0};
    char marker[600];
    if (!worker_bootstrap_path(cfg, marker)) return ERROR_TOO_LONG;
    int cursor_state = read_cursor(cfg->escola_cursor_path, cursor);
    if (cursor_state < 0) return ERROR_CONFIG;
    for (unsigned page = 0; page < 100 && !*should_stop; ++page) {
        HttpBuffer response = {0};
        char next[ESCOLA_API_CURSOR_MAX + 1] = {0};
        int rc = worker_fetch_inbox(cfg, cursor, &response, next);
        if (rc != SUCCESS) { free(response.data); return rc; }
        const char *begin, *end;
        size_t count;
        if (!worker_message_count(response.data, response.length, &begin, &end, &count)) {
            free(response.data); return ERROR_HTTP;
        }
        free(response.data);
        if (count >= ESCOLA_AUTO_PAGE_SIZE && strcmp(cursor, next) == 0)
            return ERROR_HTTP;
        if (!save_cursor(cfg->escola_cursor_path, next)) return ERROR_INTERNAL;
        memcpy(cursor, next, strlen(next) + 1);
        if (count < ESCOLA_AUTO_PAGE_SIZE) {
            (void)remove(marker);
            log_info("escola: cursor automático inicializado; mensagens antigas não foram respondidas");
            return SUCCESS;
        }
    }
    return *should_stop ? SUCCESS : ERROR_TOO_LONG;
}

static int worker_process_batch(const AppConfig *cfg)
{
    char cursor[ESCOLA_API_CURSOR_MAX + 1] = {0}, ledger[600];
    if (read_cursor(cfg->escola_cursor_path, cursor) != 1 ||
        !worker_ledger_path(cfg, ledger)) return ERROR_CONFIG;
    HttpBuffer response = {0};
    char next[ESCOLA_API_CURSOR_MAX + 1] = {0};
    int rc = worker_fetch_inbox(cfg, cursor, &response, next);
    if (rc != SUCCESS) { free(response.data); return rc; }
    const char *p, *end;
    size_t count;
    if (!worker_message_count(response.data, response.length, &p, &end, &count)) {
        free(response.data); return ERROR_HTTP;
    }
    p = json_skip_space(p);
    while (p < end && *p != ']') {
        const char *start = p;
        if (*p != '{' || !json_skip_value(&p, 0) || p > end) {
            free(response.data); return ERROR_HTTP;
        }
        rc = worker_handle_message(cfg, ledger, start, (size_t)(p - start));
        if (rc != SUCCESS) { free(response.data); return rc; }
        p = json_skip_space(p);
        if (p < end && *p == ',') { ++p; p = json_skip_space(p); }
        else if (p < end && *p != ']') { free(response.data); return ERROR_HTTP; }
    }
    free(response.data);
    if (!save_cursor(cfg->escola_cursor_path, next)) return ERROR_INTERNAL;
    (void)remove(ledger);
    return count ? SUCCESS : ERROR_NOT_FOUND;
}

static void escola_worker_loop(volatile int *should_stop)
{
    const AppConfig *current = config_current();
    if (!current || !key_is_safe(current->escola_api_key)) {
        log_info("escola: respostas automáticas desativadas (ESCOLA_API_KEY ausente)");
        return;
    }
    if (!ai_remote_enabled()) {
        log_info("escola: respostas automáticas desativadas (IA sem configuração)");
        return;
    }
    AppConfig cfg = *current;
    char cursor[ESCOLA_API_CURSOR_MAX + 1] = {0};
    int cursor_state = read_cursor(cfg.escola_cursor_path, cursor);
    char bootstrap_marker[600];
    if (cursor_state < 0 || !worker_bootstrap_path(&cfg, bootstrap_marker)) {
        log_error("escola: cursor automático inválido; worker desativado");
        return;
    }
    FILE *marker_file = fopen(bootstrap_marker, "rb");
    int bootstrap_pending = marker_file != NULL;
    if (marker_file) fclose(marker_file);
    if (cursor_state == 0 && !bootstrap_pending) {
        marker_file = fopen(bootstrap_marker, "wb");
        if (!marker_file) {
            log_error("escola: não foi possível preparar o cursor; worker desativado");
            return;
        }
        int marker_ok = fputs("1\n", marker_file) >= 0 && fflush(marker_file) == 0;
        if (fclose(marker_file) != 0) marker_ok = 0;
        if (!marker_ok) {
            log_error("escola: não foi possível preparar o cursor; worker desativado");
            return;
        }
        bootstrap_pending = 1;
    }
    if (bootstrap_pending) {
        int rc = worker_bootstrap_cursor(&cfg, should_stop);
        if (rc != SUCCESS) {
            log_warn("escola: cursor automático não inicializado (%d); não responderei a mensagens antigas", rc);
            while (!*should_stop && rc != SUCCESS) {
                if (rc == ERROR_CONFIG || rc == ERROR_NOT_IMPLEMENTED) return;
                worker_wait(should_stop, g_escola_retry_delay
                    ? g_escola_retry_delay : ESCOLA_AUTO_POLL_SECONDS);
                g_escola_retry_delay = 0;
                rc = worker_bootstrap_cursor(&cfg, should_stop);
            }
            if (*should_stop) return;
        }
    }
    log_info("escola: tutoria automática ativa; verificará mensagens a cada %u segundos",
             ESCOLA_AUTO_POLL_SECONDS);
    while (!*should_stop) {
        int rc = worker_process_batch(&cfg);
        if (rc == ERROR_AI)
            log_warn("escola: serviço de IA indisponível; mensagem pendente será tentada novamente");
        else if (rc != SUCCESS && rc != ERROR_NOT_FOUND)
            log_warn("escola: processamento automático será tentado novamente (erro interno %d)", rc);
        if (rc == ERROR_CONFIG || rc == ERROR_NOT_IMPLEMENTED) {
            log_error("escola: API recusou configuração ou rota; respostas automáticas desativadas");
            return;
        }
        unsigned delay = g_escola_retry_delay ? g_escola_retry_delay : ESCOLA_AUTO_POLL_SECONDS;
        g_escola_retry_delay = 0;
        worker_wait(should_stop, delay);
    }
}

#if defined(_WIN32)
static unsigned __stdcall escola_worker_entry(void *arg)
{
    escola_worker_loop((volatile int *)arg);
    return 0;
}
#else
static void *escola_worker_entry(void *arg)
{
    escola_worker_loop((volatile int *)arg);
    return NULL;
}
#endif

int escola_api_worker_start(volatile int *should_stop)
{
    if (!should_stop || g_escola_worker_started) return ERROR_INVALID_INPUT;
    g_escola_server_curl = 1;
#if defined(_WIN32)
    uintptr_t handle = _beginthreadex(NULL, 0, escola_worker_entry,
                                      (void *)should_stop, 0, NULL);
    if (!handle) return ERROR_INTERNAL;
    g_escola_worker_thread = (HANDLE)handle;
#else
    if (pthread_create(&g_escola_worker_thread, NULL, escola_worker_entry,
                       (void *)should_stop) != 0) return ERROR_INTERNAL;
#endif
    g_escola_worker_started = 1;
    return SUCCESS;
}

void escola_api_worker_join(void)
{
    if (!g_escola_worker_started) return;
#if defined(_WIN32)
    (void)WaitForSingleObject(g_escola_worker_thread, INFINITE);
    CloseHandle(g_escola_worker_thread);
    g_escola_worker_thread = NULL;
#else
    (void)pthread_join(g_escola_worker_thread, NULL);
#endif
    g_escola_worker_started = 0;
}
#else
int escola_api_worker_start(volatile int *should_stop)
{
    (void)should_stop;
    return ERROR_NOT_IMPLEMENTED;
}

void escola_api_worker_join(void) {}
#endif
