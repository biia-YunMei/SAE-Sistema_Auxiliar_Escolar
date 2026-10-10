/* logger.c
 * Implementação do logger.
 *
 * Recursos:
 *   - thread-safe (mutex)
 *   - rotação diária por nome de arquivo
 *   - níveis filtráveis
 *   - nunca bloqueia o programa se o disco falhar
 */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#if defined(_WIN32)
#include <direct.h>
#endif

/* Limite máximo da mensagem após formatação (evita explosão de log). */
#define LOG_MAX_MESSAGE 2048

static struct {
    char        dir[256];
    char        current_date[16];   /* YYYYMMDD */
    FILE       *fp;
    LogLevel    min_level;
    pthread_mutex_t mu;
    int         initialized;
} g_log = {
    .current_date = {0},
    .fp           = NULL,
    .min_level    = LOG_INFO,
    .initialized  = 0,
    .mu           = PTHREAD_MUTEX_INITIALIZER
};

static const char *level_str(LogLevel l)
{
    switch (l) {
    case LOG_DEBUG: return "DEBUG";
    case LOG_INFO:  return "INFO";
    case LOG_WARN:  return "WARNING";
    case LOG_ERROR: return "ERROR";
    }
    return "?";
}

static int make_dir(const char *path)
{
#if defined(_WIN32)
    return _mkdir(path);
#else
    return mkdir(path, 0700);
#endif
}

/* Garante que o diretório de logs existe. */
static int ensure_dir(const char *path)
{
    if (!path || !*path) return -1;
    struct stat st;
    if (stat(path, &st) == 0) return S_ISDIR(st.st_mode) ? 0 : -1;
    /* mkdir -p manual (Windows não tem mkdir -p). */
    char tmp[512];
    size_t n = strlen(path);
    if (n >= sizeof tmp) return -1;
    memcpy(tmp, path, n + 1);
    for (size_t i = 1; i < n; i++) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            char saved = tmp[i];
            tmp[i] = '\0';
            (void)make_dir(tmp);         /* ignora erro: pode já existir */
            tmp[i] = saved;
        }
    }
    (void)make_dir(tmp);
    return 0;
}

static void open_today_locked(void)
{
    time_t now = time(NULL);
    struct tm tm;
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char datebuf[16];
    strftime(datebuf, sizeof datebuf, "%Y%m%d", &tm);

    if (g_log.fp && strcmp(g_log.current_date, datebuf) == 0) return;

    if (g_log.fp) { fclose(g_log.fp); g_log.fp = NULL; }

    char path[512];
    snprintf(path, sizeof path, "%s/school-%s.log",
             g_log.dir[0] ? g_log.dir : "logs", datebuf);

    g_log.fp = fopen(path, "a");
    if (g_log.fp) {
#if !defined(_WIN32)
        (void)chmod(path, 0600);
#endif
        /* Line-buffered: cada log vai pro disco em uma linha. */
        setvbuf(g_log.fp, NULL, _IOLBF, 0);
        strncpy(g_log.current_date, datebuf, sizeof g_log.current_date);
    }
}

int logger_init(const char *log_dir, LogLevel min_level)
{
    pthread_mutex_lock(&g_log.mu);
    if (log_dir && *log_dir) {
        strncpy(g_log.dir, log_dir, sizeof g_log.dir - 1);
        g_log.dir[sizeof g_log.dir - 1] = '\0';
    } else {
        strncpy(g_log.dir, "logs", sizeof g_log.dir - 1);
    }
    ensure_dir(g_log.dir);
#if !defined(_WIN32)
    (void)chmod(g_log.dir, 0700);
#endif
    g_log.min_level = min_level;
    open_today_locked();
    g_log.initialized = 1;
    pthread_mutex_unlock(&g_log.mu);
    return 0;
}

void logger_shutdown(void)
{
    pthread_mutex_lock(&g_log.mu);
    if (g_log.fp) { fclose(g_log.fp); g_log.fp = NULL; }
    g_log.initialized = 0;
    pthread_mutex_unlock(&g_log.mu);
}

void logger_set_level(LogLevel l) { g_log.min_level = l; }
LogLevel logger_get_level(void)   { return g_log.min_level; }

void log_message(LogLevel level, const char *file, int line, const char *fmt, ...)
{
    if (!g_log.initialized) {
        /* Inicialização preguiçosa com defaults. */
        logger_init("logs", LOG_INFO);
    }
    if (level < g_log.min_level) return;

    char msg[LOG_MAX_MESSAGE];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(msg, sizeof msg, fmt ? fmt : "", ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof msg) n = (int)sizeof msg - 1;
    msg[n] = '\0';

    time_t now = time(NULL);
    struct tm tm;
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char ts[32];
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm);

    const char *short_file = file ? strrchr(file, '/') : NULL;
#if defined(_WIN32)
    if (!short_file) short_file = strrchr(file, '\\');
#endif
    short_file = short_file ? short_file + 1 : file;

    pthread_mutex_lock(&g_log.mu);
    open_today_locked();

    /* Saída para stdout (cobre ambientes containerizados) */
    fprintf(stdout, "[%s] [%s] [%s:%d] %s\n",
            ts, level_str(level),
            short_file ? short_file : "?", line, msg);
    fflush(stdout);

    /* Saída para arquivo de auditoria */
    if (g_log.fp) {
        fprintf(g_log.fp, "[%s] [%s] [%s:%d] %s\n",
                ts, level_str(level),
                short_file ? short_file : "?", line, msg);
    }
    pthread_mutex_unlock(&g_log.mu);

    /* Silencia warning de "unused" em alguns toolchains */
    (void)errno;
}
