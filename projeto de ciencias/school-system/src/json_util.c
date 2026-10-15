#include "json_util.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const unsigned char *p; } JsonParser;

static void ws(JsonParser *p)
{
    while (*p->p == ' ' || *p->p == '\t' || *p->p == '\r' || *p->p == '\n') ++p->p;
}

static int hex4(JsonParser *p, uint32_t *out)
{
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        unsigned char c = *p->p++;
        unsigned digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10U;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10U;
        else return 0;
        value = (value << 4) | digit;
    }
    *out = value;
    return 1;
}

static int utf8_append(char *out, size_t cap, size_t *used, uint32_t cp)
{
    if (cp == 0 || cp > 0x10ffff) return 0;
    unsigned char bytes[4]; size_t n;
    if (cp <= 0x7f) { bytes[0] = (unsigned char)cp; n = 1; }
    else if (cp <= 0x7ff) {
        bytes[0] = (unsigned char)(0xc0 | (cp >> 6));
        bytes[1] = (unsigned char)(0x80 | (cp & 0x3f)); n = 2;
    } else if (cp <= 0xffff) {
        bytes[0] = (unsigned char)(0xe0 | (cp >> 12));
        bytes[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
        bytes[2] = (unsigned char)(0x80 | (cp & 0x3f)); n = 3;
    } else {
        bytes[0] = (unsigned char)(0xf0 | (cp >> 18));
        bytes[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3f));
        bytes[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
        bytes[3] = (unsigned char)(0x80 | (cp & 0x3f)); n = 4;
    }
    if (out) {
        if (n >= cap || *used > cap - n - 1) return 0;
        memcpy(out + *used, bytes, n);
    }
    *used += n;
    return 1;
}

/* Strings JSON chegam em UTF-8. Bytes não escapados precisam ser copiados
 * como estão; só sequências \uXXXX devem passar por utf8_append(). */
static int utf8_copy_raw(JsonParser *p, unsigned char lead,
                         char *out, size_t cap, size_t *used)
{
    const unsigned char *start = p->p - 1;
    size_t length;
    if (lead < 0x80) length = 1;
    else if (lead >= 0xc2 && lead <= 0xdf) length = 2;
    else if (lead >= 0xe0 && lead <= 0xef) length = 3;
    else if (lead >= 0xf0 && lead <= 0xf4) length = 4;
    else return 0;

    for (size_t i = 1; i < length; ++i)
        if (start[i] == '\0' || (start[i] & 0xc0) != 0x80) return 0;
    if ((lead == 0xe0 && start[1] < 0xa0) ||
        (lead == 0xed && start[1] >= 0xa0) ||
        (lead == 0xf0 && start[1] < 0x90) ||
        (lead == 0xf4 && start[1] > 0x8f)) return 0;
    if (out && (used == NULL || *used > cap || length >= cap - *used)) return 0;
    if (out) memcpy(out + *used, start, length);
    if (used == NULL || *used > SIZE_MAX - length) return 0;
    *used += length;
    p->p += length - 1;
    return 1;
}

static int parse_string(JsonParser *p, char *out, size_t cap)
{
    if (*p->p++ != '"') return 0;
    size_t used = 0;
    while (*p->p && *p->p != '"') {
        uint32_t cp;
        unsigned char c = *p->p++;
        if (c < 0x20) return 0;
        if (c != '\\') {
            if (!utf8_copy_raw(p, c, out, cap, &used)) return 0;
            continue;
        }
        c = *p->p++;
        switch (c) {
        case '"': case '\\': case '/': cp = c; break;
        case 'b': cp = '\b'; break;
        case 'f': cp = '\f'; break;
        case 'n': cp = '\n'; break;
        case 'r': cp = '\r'; break;
        case 't': cp = '\t'; break;
        case 'u':
            if (!hex4(p, &cp)) return 0;
            if (cp >= 0xd800 && cp <= 0xdbff) {
                uint32_t low;
                if (p->p[0] != '\\' || p->p[1] != 'u') return 0;
                p->p += 2;
                if (!hex4(p, &low) || low < 0xdc00 || low > 0xdfff) return 0;
                cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
            } else if (cp >= 0xdc00 && cp <= 0xdfff) return 0;
            break;
        default: return 0;
        }
        if (!utf8_append(out, cap, &used, cp)) return 0;
    }
    if (*p->p++ != '"') return 0;
    if (out) out[used] = '\0';
    return 1;
}

static int skip_value(JsonParser *p, unsigned depth);

static int skip_array(JsonParser *p, unsigned depth)
{
    ++p->p; ws(p);
    if (*p->p == ']') { ++p->p; return 1; }
    for (;;) {
        if (!skip_value(p, depth + 1)) return 0;
        ws(p);
        if (*p->p == ']') { ++p->p; return 1; }
        if (*p->p++ != ',') return 0;
        ws(p);
    }
}

static int skip_object(JsonParser *p, unsigned depth)
{
    ++p->p; ws(p);
    if (*p->p == '}') { ++p->p; return 1; }
    for (;;) {
        if (!parse_string(p, NULL, 0)) return 0;
        ws(p);
        if (*p->p++ != ':') return 0;
        ws(p);
        if (!skip_value(p, depth + 1)) return 0;
        ws(p);
        if (*p->p == '}') { ++p->p; return 1; }
        if (*p->p++ != ',') return 0;
        ws(p);
    }
}

static int skip_value(JsonParser *p, unsigned depth)
{
    if (depth > 64) return 0;
    ws(p);
    if (*p->p == '"') return parse_string(p, NULL, 0);
    if (*p->p == '{') return skip_object(p, depth);
    if (*p->p == '[') return skip_array(p, depth);
    const char *literal = NULL;
    if (*p->p == 't') literal = "true";
    else if (*p->p == 'f') literal = "false";
    else if (*p->p == 'n') literal = "null";
    if (literal) {
        size_t n = strlen(literal);
        if (strncmp((const char *)p->p, literal, n) != 0) return 0;
        p->p += n; return 1;
    }
    if (*p->p == '-') ++p->p;
    if (*p->p == '0') ++p->p;
    else {
        if (*p->p < '1' || *p->p > '9') return 0;
        while (isdigit(*p->p)) ++p->p;
    }
    if (*p->p == '.') {
        ++p->p; if (!isdigit(*p->p)) return 0;
        while (isdigit(*p->p)) ++p->p;
    }
    if (*p->p == 'e' || *p->p == 'E') {
        ++p->p; if (*p->p == '+' || *p->p == '-') ++p->p;
        if (!isdigit(*p->p)) return 0;
        while (isdigit(*p->p)) ++p->p;
    }
    return 1;
}

static int object_find(const char *json, const char *key, char *string_out,
                       size_t cap, int64_t *int_out, int want_int)
{
    if (!json || !key || (!want_int && (!string_out || cap == 0))) return 0;
    JsonParser p = { (const unsigned char *)json };
    ws(&p);
    if (*p.p++ != '{') return 0;
    ws(&p);
    int found = 0, valid = 1;
    if (*p.p != '}') for (;;) {
        char name[128];
        if (!parse_string(&p, name, sizeof name)) { valid = 0; break; }
        ws(&p);
        if (*p.p++ != ':') { valid = 0; break; }
        ws(&p);
        int match = strcmp(name, key) == 0;
        if (match && found) { valid = 0; break; }
        if (match) found = 1;
        if (!want_int && match) {
            if (*p.p != '"' || !parse_string(&p, string_out, cap)) { valid = 0; break; }
        } else if (want_int && match) {
            const unsigned char *start = p.p;
            if (*p.p == '-') ++p.p;
            if (*p.p == '0') ++p.p;
            else {
                if (*p.p < '1' || *p.p > '9') { valid = 0; break; }
                while (isdigit(*p.p)) ++p.p;
            }
            if (*p.p == '.' || *p.p == 'e' || *p.p == 'E') { valid = 0; break; }
            errno = 0;
            char *end = NULL;
            long long value = strtoll((const char *)start, &end, 10);
            if (errno == ERANGE || end != (const char *)p.p) { valid = 0; break; }
            *int_out = (int64_t)value;
        } else if (!skip_value(&p, 1)) { valid = 0; break; }
        ws(&p);
        if (*p.p == '}') break;
        if (*p.p++ != ',') { valid = 0; break; }
        ws(&p);
    }
    if (!valid || *p.p++ != '}') return 0;
    ws(&p);
    return found && *p.p == '\0';
}

int json_object_get_string(const char *json, const char *key, char *out, size_t cap)
{
    return object_find(json, key, out, cap, NULL, 0);
}

int json_object_get_int64(const char *json, const char *key, int64_t *out)
{
    return out && object_find(json, key, NULL, 0, out, 1);
}

int parse_int64_strict(const char *text, int64_t *out)
{
    if (!text || !*text || !out) return 0;
    errno = 0;
    char *end = NULL;
    long long value = strtoll(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0') return 0;
    *out = (int64_t)value;
    return 1;
}
