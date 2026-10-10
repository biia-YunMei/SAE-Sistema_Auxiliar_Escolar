/* image.c
 * Não há provedor de análise de imagem configurado neste adaptador.
 */
#include "image.h"
#include "logger.h"

int image_analyze(const char *image_path, const char *question,
                  char *out_response, size_t cap)
{
    if (!image_path || !out_response || cap == 0) return ERROR_INVALID_INPUT;
    out_response[0] = '\0';
    (void)question;
    log_warn("image: nenhum provedor de visão está configurado");
    return ERROR_NOT_IMPLEMENTED;
}
