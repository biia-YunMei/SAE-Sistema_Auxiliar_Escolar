/* image.h
 * Abstração para análise de imagens (IA visão).
 */
#ifndef SCHOOL_IMAGE_H
#define SCHOOL_IMAGE_H

#include "error.h"
#include <stddef.h>

int image_analyze(const char *image_path, const char *question,
                  char *out_response, size_t cap);

#endif /* SCHOOL_IMAGE_H */
