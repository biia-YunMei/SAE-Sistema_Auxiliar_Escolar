/* image.c
 * Stub da análise de imagem.
 */
#include "image.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

int image_analyze(const char *image_path, const char *question,
                  char *out_response, size_t cap)
{
    if (!image_path || !out_response || cap < 16) return ERROR_INVALID_INPUT;
    struct stat st;
    if (stat(image_path, &st) != 0) {
        log_warn("image: arquivo inexistente: %s", image_path);
        return ERROR_NOT_FOUND;
    }
    snprintf(out_response, cap,
        "[stub:visão sobre %s] — descreva o que vê e pergunte por etapas.",
        question ? question : "exercício");
    return SUCCESS;
}
