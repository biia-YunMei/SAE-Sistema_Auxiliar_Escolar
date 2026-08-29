/* validation.c
 * Implementação dos validadores.
 */
#include "validation.h"

#include <ctype.h>
#include <string.h>
#include <stdio.h>

bool validation_is_valid_phone(const char *phone)
{
    if (!phone) return false;
    const char *p = phone;
    if (*p == '+') p++;

    size_t digits = 0;
    while (*p) {
        if (!isdigit((unsigned char)*p)) return false;
        digits++;
        p++;
    }
    /* Regras: 8 a 15 dígitos (padrão ITU-T E.164). */
    return digits >= 8 && digits <= 15;
}

bool validation_is_valid_date(const char *date)
{
    if (!date) return false;
    if (strlen(date) != 10) return false;
    if (date[4] != '-' || date[7] != '-') return false;
    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) continue;
        if (!isdigit((unsigned char)date[i])) return false;
    }
    int y = (date[0]-'0')*1000 + (date[1]-'0')*100 +
            (date[2]-'0')*10  + (date[3]-'0');
    int m = (date[5]-'0')*10  + (date[6]-'0');
    int d = (date[8]-'0')*10  + (date[9]-'0');
    if (y < 1970 || y > 2100) return false;
    if (m < 1 || m > 12) return false;
    if (d < 1 || d > 31) return false;
    /* Validação simplificada de dias por mês. */
    static const int dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int maxd = dim[m-1];
    if (m == 2) {
        int leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
        if (leap) maxd = 29;
    }
    return d <= maxd;
}

bool validation_is_valid_time(const char *time)
{
    if (!time) return false;
    if (strlen(time) != 5) return false;
    if (time[2] != ':') return false;
    if (!isdigit((unsigned char)time[0]) || !isdigit((unsigned char)time[1]) ||
        !isdigit((unsigned char)time[3]) || !isdigit((unsigned char)time[4]))
        return false;
    int hh = (time[0]-'0')*10 + (time[1]-'0');
    int mm = (time[3]-'0')*10 + (time[4]-'0');
    return hh >= 0 && hh < 24 && mm >= 0 && mm < 60;
}

bool validation_is_safe_text(const char *text)
{
    if (!text) return false;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        unsigned char c = *p;
        if (c < 0x20) return false;               /* controles */
        if (c == 0x7f) return false;              /* DEL */
    }
    return true;
}

bool validation_length_between(const char *s, size_t min, size_t max)
{
    if (!s) return false;
    size_t n = strlen(s);
    return n >= min && n <= max;
}

size_t validation_sanitize_text(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0) return 0;
    if (!src) { dst[0] = '\0'; return 0; }
    size_t out = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p; p++) {
        unsigned char c = *p;
        if (c < 0x20 || c == 0x7f) c = ' ';
        if (out + 1 >= cap) break;
        dst[out++] = (char)c;
    }
    dst[out] = '\0';
    return out;
}
