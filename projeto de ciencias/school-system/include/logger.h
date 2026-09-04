/* logger.h
 * Sistema de logs com níveis e saída em arquivo + stdout.
 *
 *   log_info("Professor autenticado");
 *   log_warn("Usuário não autorizado");
 *   log_error("Falha no banco: %s", sqlite3_errmsg(db));
 *
 * Os logs vão para:
 *   - stdout (para docker/k8s/journald)
 *   - <log_dir>/school-YYYYMMDD.log (auditoria)
 *
 * IMPORTANTE: nunca logar senhas, tokens, conteúdo completo de mensagens
 * de alunos/professores. Se necessário, logue apenas o tamanho e um hash.
 */
#ifndef SCHOOL_LOGGER_H
#define SCHOOL_LOGGER_H

typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO  = 1,
    LOG_WARN  = 2,
    LOG_ERROR = 3
} LogLevel;

int  logger_init(const char *log_dir, LogLevel min_level);
void logger_shutdown(void);

/* Define o nível mínimo em tempo de execução (sem reinicializar). */
void logger_set_level(LogLevel level);
LogLevel logger_get_level(void);

void log_message(LogLevel level, const char *file, int line,
                 const char *fmt, ...);

/* Macros de conveniência. Capturam __FILE__ e __LINE__ automaticamente. */
#define log_debug(...) log_message(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define log_info(...)  log_message(LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define log_warn(...)  log_message(LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define log_error(...) log_message(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)

#endif /* SCHOOL_LOGGER_H */
