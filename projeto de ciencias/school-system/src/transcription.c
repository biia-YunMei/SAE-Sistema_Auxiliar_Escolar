/* transcription.c
 * Não há provedor de transcrição configurado neste adaptador.
 */
#include "transcription.h"
#include "logger.h"

int transcription_transcribe(const char *audio_path, char *out_text, size_t cap)
{
    if (!audio_path || !out_text || cap == 0) return ERROR_INVALID_INPUT;
    out_text[0] = '\0';
    log_warn("transcription: nenhum provedor de áudio está configurado");
    return ERROR_NOT_IMPLEMENTED;
}
