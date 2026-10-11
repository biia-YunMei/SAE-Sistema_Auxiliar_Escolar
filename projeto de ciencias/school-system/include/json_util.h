#ifndef SCHOOL_JSON_UTIL_H
#define SCHOOL_JSON_UTIL_H

#include <stddef.h>
#include <stdint.h>

/* Reads a top-level JSON member, validating the complete document. */
int json_object_get_string(const char *json, const char *key, char *out, size_t cap);
int json_object_get_int64(const char *json, const char *key, int64_t *out);
int parse_int64_strict(const char *text, int64_t *out);

#endif
