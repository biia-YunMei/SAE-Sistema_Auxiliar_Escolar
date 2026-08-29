/* error.c
 * Implementação das mensagens de erro.
 */
#include "error.h"

const char *error_string(ErrorCode code)
{
    switch (code) {
    case SUCCESS:                return "success";

    case ERROR_INVALID_INPUT:    return "invalid input";
    case ERROR_MISSING_FIELD:    return "missing field";
    case ERROR_TOO_LONG:         return "value too long";
    case ERROR_INVALID_PHONE:    return "invalid phone number";
    case ERROR_INVALID_DATE:     return "invalid date";
    case ERROR_INVALID_ID:       return "invalid id";

    case ERROR_UNAUTHORIZED:     return "unauthorized";
    case ERROR_FORBIDDEN:        return "forbidden";
    case ERROR_INVALID_CREDENTIALS: return "invalid credentials";
    case ERROR_SESSION_EXPIRED:  return "session expired";

    case ERROR_NOT_FOUND:        return "not found";
    case ERROR_ALREADY_EXISTS:   return "already exists";
    case ERROR_CONFLICT:         return "conflict";

    case ERROR_DATABASE:         return "database error";
    case ERROR_NETWORK:          return "network error";
    case ERROR_HTTP:             return "http error";
    case ERROR_AI:               return "ai service error";
    case ERROR_WHATSAPP:         return "whatsapp service error";
    case ERROR_TRANSCRIPTION:    return "transcription error";
    case ERROR_INTERNAL:         return "internal error";

    case ERROR_CONFIG:           return "configuration error";
    case ERROR_NOT_IMPLEMENTED:  return "not implemented";
    }
    return "unknown error";
}
