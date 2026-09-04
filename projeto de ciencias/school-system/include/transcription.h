/* transcription.h
 * Abstração para transcrição de áudio. Integração fica atrás da flag
 * de compilação; o stub cobre o pipeline básico para a feira.
 */
#ifndef SCHOOL_TRANSCRIPTION_H
#define SCHOOL_TRANSCRIPTION_H

#include "error.h"
#include <stddef.h>

int transcription_transcribe(const char *audio_path,
                             char *out_text, size_t cap);

#endif /* SCHOOL_TRANSCRIPTION_H */
