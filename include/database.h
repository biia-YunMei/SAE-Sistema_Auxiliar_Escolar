/* database.h
 * Camada de persistência SQLite.
 *
 * - Conexão thread-safe (uma por thread ou pool pequeno).
 * - Migrations idempotentes controladas por tabela schema_version.
 * - Helpers de prepared statements (consultas parametrizadas, nunca
 *   concatenação de strings).
 * - Transações explícitas.
 *
 * Regras:
 *   - Nenhum SQL com sprintf.
 *   - Toda query externa passa por db_prepare.
 *   - Erros são reportados via ErrorCode + log; nunca abort().
 */
#ifndef SCHOOL_DATABASE_H
#define SCHOOL_DATABASE_H

#include "error.h"
#include <stdbool.h>
#include <sqlite3.h>

/* Inicializa o banco no caminho indicado. Cria schema se necessário. */
int  database_init(const char *path);
void database_shutdown(void);

/* Obtém uma conexão (a atual thread tem sua própria conexão). */
sqlite3 *database_get(void);

/* Cria prepared statement seguro. */
int db_prepare(const char *sql, sqlite3_stmt **out);

/* Vincula parâmetros texto com checagem de tamanho. */
int db_bind_text (sqlite3_stmt *s, int idx, const char *value);
int db_bind_int  (sqlite3_stmt *s, int idx, int value);
int db_bind_int64(sqlite3_stmt *s, int idx, long long value);
int db_bind_null (sqlite3_stmt *s, int idx);

/* Executa um statement sem retorno (INSERT/UPDATE/DELETE/Schema). */
int db_exec(const char *sql);

/* Transações. */
int db_begin(void);
int db_commit(void);
int db_rollback(void);

/* Última mensagem de erro (apenas para logs). */
const char *database_last_error(void);

#endif /* SCHOOL_DATABASE_H */
