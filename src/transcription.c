/* transcription.c
 * Stub da transcrição.
 *
 * Em produção: POST multipart para o serviço configurado (Whisper, etc).
 * Aqui só validamos que o arquivo existe e devolvemos um placeholder.
 */
#include "transcription.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int transcription_transcribe(const char *audio_path, char *out_text, size_t cap)
{
    if (!audio_path || !out_text || cap < 16) return ERROR_INVALID_INPUT;

    struct stat st;
    if (stat(audio_path, &st) != 0) {
        log_warn("transcription: arquivo inexistente: %s", audio_path);
        return ERROR_NOT_FOUND;
    }
    snprintf(out_text, cap, "[stub:transcrição de %s]", audio_path);
    return SUCCESS;
}
