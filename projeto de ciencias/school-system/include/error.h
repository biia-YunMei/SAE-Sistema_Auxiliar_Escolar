/* error.h
 * Códigos de erro padronizados em todo o back-end.
 * Manter estável: a UI e os logs dependem destas mensagens.
 */
#ifndef SCHOOL_ERROR_H
#define SCHOOL_ERROR_H

typedef enum {
    SUCCESS = 0,

    /* Entradas e validação */
    ERROR_INVALID_INPUT      = 100,
    ERROR_MISSING_FIELD      = 101,
    ERROR_TOO_LONG           = 102,
    ERROR_INVALID_PHONE      = 103,
    ERROR_INVALID_DATE       = 104,
    ERROR_INVALID_ID         = 105,

    /* Autenticação / autorização */
    ERROR_UNAUTHORIZED       = 200,
    ERROR_FORBIDDEN          = 201,
    ERROR_INVALID_CREDENTIALS= 202,
    ERROR_SESSION_EXPIRED    = 203,

    /* Negócio */
    ERROR_NOT_FOUND          = 300,
    ERROR_ALREADY_EXISTS     = 301,
    ERROR_CONFLICT           = 302,

    /* Infra */
    ERROR_DATABASE           = 400,
    ERROR_NETWORK            = 401,
    ERROR_HTTP               = 402,
    ERROR_AI                 = 403,
    ERROR_WHATSAPP           = 404,
    ERROR_TRANSCRIPTION      = 405,
    ERROR_INTERNAL           = 499,

    /* Configuração */
    ERROR_CONFIG             = 500,
    ERROR_NOT_IMPLEMENTED    = 501
} ErrorCode;

/* Converte código em string curta, útil para logs e respostas JSON. */
const char *error_string(ErrorCode code);

#endif /* SCHOOL_ERROR_H */
