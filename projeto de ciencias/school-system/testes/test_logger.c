/* test_logger.c
 * Testes do logger. Cria um diretório temporário e confere saída.
 */
#include "logger.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#if defined(_WIN32)
#include <direct.h>
#endif

static int passed = 0, failed = 0;
#define CHECK(expr) do { \
    if (expr) { passed++; } \
    else { failed++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #expr); } \
} while (0)

static long file_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (long)st.st_size;
}

static int make_dir(const char *path)
{
#if defined(_WIN32)
    return _mkdir(path);
#else
    return mkdir(path, 0755);
#endif
}

int main(void)
{
    const char *dir = "logs/test";
    (void)make_dir("logs");
    (void)make_dir(dir);

    logger_init(dir, LOG_DEBUG);
    log_debug("linha debug");
    log_info("linha info");
    log_warn("linha warn");
    log_error("linha error com %d", 42);
    logger_shutdown();

    time_t now = time(NULL);
    struct tm today;
    char date[16] = {0}, path[256] = {0};
#if defined(_WIN32)
    CHECK(localtime_s(&today, &now) == 0);
#else
    CHECK(localtime_r(&now, &today) != NULL);
#endif
    CHECK(strftime(date, sizeof date, "%Y%m%d", &today) > 0);
    snprintf(path, sizeof path, "%s/school-%s.log", dir, date);
    CHECK(file_size(path) > 0);

    CHECK(logger_get_level() == LOG_DEBUG);
    printf("\nLogger: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
