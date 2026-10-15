#ifndef SAE_ANONYMOUS_ENROLLMENT_H
#define SAE_ANONYMOUS_ENROLLMENT_H

#include "error.h"
#include <stdint.h>

int anonymous_enrollment_create(int64_t coordinator_id, int64_t class_id,
                                char code_out[17]);
int anonymous_enrollment_claim(const char *sender_ref, const char *code,
                               int64_t *class_id_out);
int anonymous_teacher_link_create(int64_t teacher_user_id, char code_out[17]);
int anonymous_teacher_link_claim(const char *sender_ref, const char *code);
int anonymous_teacher_link_user(const char *sender_ref, int64_t *user_id_out);

#endif
