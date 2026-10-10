/* database.c
 * Implementação da camada de banco (SQLite).
 *
 * SQLite é thread-safe em modo serialized (padrão moderno), mas ainda
 * assim preferimos uma conexão por thread via pthread_key_t para isolar
 * transações e melhorar a localidade.
 */
#include "database.h"
#include "logger.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <io.h>
#define chmod _chmod
#else
#include <sys/stat.h>
#endif

/* ===================== estado global ============================== */

static pthread_key_t  g_db_key;
static pthread_once_t  g_db_once = PTHREAD_ONCE_INIT;
static char            g_path[512] = {0};
static int             g_initialized = 0;

/* ===================== helpers de conexão ========================= */

static void close_db(void *arg)
{
    if (arg) sqlite3_close((sqlite3 *)arg);
}

static void init_key(void)
{
    pthread_key_create(&g_db_key, close_db);
}

static sqlite3 *open_connection(const char *path)
{
    sqlite3 *db = NULL;
    int rc = sqlite3_open(path, &db);
    if (rc != SQLITE_OK) {
        log_error("sqlite3_open falhou: %s",
                  db ? sqlite3_errmsg(db) : "sem memória");
        if (db) sqlite3_close(db);
        return NULL;
    }
    /* Foreign keys devem ser habilitadas por conexão. */
    sqlite3_exec(db, "PRAGMA foreign_keys = ON;",  NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA journal_mode = WAL;", NULL, NULL, NULL);
    sqlite3_busy_timeout(db, 5000);
    return db;
}

sqlite3 *database_get(void)
{
    pthread_once(&g_db_once, init_key);
    sqlite3 *db = (sqlite3 *)pthread_getspecific(g_db_key);
    if (db) return db;
    db = open_connection(g_path[0] ? g_path : ":memory:");
    if (db) pthread_setspecific(g_db_key, db);
    return db;
}

const char *database_last_error(void)
{
    sqlite3 *db = database_get();
    return db ? sqlite3_errmsg(db) : "no connection";
}

/* ===================== migrations ================================ */

/* Cada migration roda em uma transação.
 * O número de versão fica em schema_version. */
static int get_version(int *out)
{
    sqlite3 *db = database_get();
    if (!db) return ERROR_DATABASE;

    /* Tabela de controle — criada se não existir. */
    char *errmsg = NULL;
    int rc = sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS schema_version (version INTEGER PRIMARY KEY);",
        NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        log_error("create schema_version: %s", errmsg);
        sqlite3_free(errmsg);
        return ERROR_DATABASE;
    }
    sqlite3_stmt *st = NULL;
    rc = sqlite3_prepare_v2(db,
                            "SELECT COALESCE(MAX(version), 0) FROM schema_version;",
                            -1, &st, NULL);
    if (rc != SQLITE_OK) return ERROR_DATABASE;
    *out = 0;
    if (sqlite3_step(st) == SQLITE_ROW)
        *out = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return SUCCESS;
}

static int set_version(int v)
{
    sqlite3 *db = database_get();
    if (!db) return ERROR_DATABASE;
    sqlite3_stmt *st = NULL;
    int rc = sqlite3_prepare_v2(db,
        "INSERT INTO schema_version(version) VALUES(?);", -1, &st, NULL);
    if (rc != SQLITE_OK) return ERROR_DATABASE;
    sqlite3_bind_int(st, 1, v);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? SUCCESS : ERROR_DATABASE;
}

/* Cada bloco é uma migration. Se a versão atual >= n, é pulada. */
static int run_migrations(void)
{
    int cur = 0;
    if (get_version(&cur) != SUCCESS) return ERROR_DATABASE;
    log_info("Schema versão atual: %d", cur);

    struct { int v; const char *sql; } migs[] = {
        { 1,
          "CREATE TABLE IF NOT EXISTS users ("
          "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
          "  name TEXT NOT NULL,"
          "  phone TEXT NOT NULL UNIQUE,"
          "  password_hash TEXT,"
          "  user_type TEXT NOT NULL CHECK (user_type IN ('COORD','PROF','STUD')),"
          "  status TEXT NOT NULL DEFAULT 'ACTIVE' "
          "    CHECK (status IN ('ACTIVE','INACTIVE','BLOCKED')),"
          "  created_at TEXT NOT NULL DEFAULT (datetime('now'))"
          ");"
        },
        { 2,
          "CREATE TABLE IF NOT EXISTS classes ("
          "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
          "  name TEXT NOT NULL UNIQUE,"
          "  grade TEXT NOT NULL,"
          "  shift TEXT NOT NULL CHECK (shift IN ('MORNING','AFTERNOON','EVENING','FULL')),"
          "  status TEXT NOT NULL DEFAULT 'ACTIVE',"
          "  created_at TEXT NOT NULL DEFAULT (datetime('now'))"
          ");"
        },
        { 3,
          "CREATE TABLE IF NOT EXISTS teachers ("
          "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
          "  user_id INTEGER NOT NULL UNIQUE,"
          "  registration_number TEXT UNIQUE,"
          "  FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE"
          ");"
        },
        { 4,
          "CREATE TABLE IF NOT EXISTS students ("
          "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
          "  user_id INTEGER NOT NULL UNIQUE,"
          "  class_id INTEGER,"
          "  registration_number TEXT UNIQUE,"
          "  FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,"
          "  FOREIGN KEY (class_id) REFERENCES classes(id) ON DELETE SET NULL"
          ");"
        },
        { 5,
          "CREATE TABLE IF NOT EXISTS teacher_classes ("
          "  teacher_id INTEGER NOT NULL,"
          "  class_id   INTEGER NOT NULL,"
          "  PRIMARY KEY (teacher_id, class_id),"
          "  FOREIGN KEY (teacher_id) REFERENCES teachers(id) ON DELETE CASCADE,"
          "  FOREIGN KEY (class_id)   REFERENCES classes(id)  ON DELETE CASCADE"
          ");"
        },
        { 6,
          "CREATE TABLE IF NOT EXISTS tasks ("
          "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
          "  teacher_id INTEGER NOT NULL,"
          "  class_id   INTEGER NOT NULL,"
          "  subject    TEXT NOT NULL,"
          "  title      TEXT NOT NULL,"
          "  description TEXT,"
          "  due_date   TEXT NOT NULL,"
          "  status     TEXT NOT NULL DEFAULT 'ACTIVE'"
          "    CHECK (status IN ('ACTIVE','ARCHIVED')),"
          "  created_at TEXT NOT NULL DEFAULT (datetime('now')),"
          "  updated_at TEXT NOT NULL DEFAULT (datetime('now')),"
          "  FOREIGN KEY (teacher_id) REFERENCES teachers(id) ON DELETE CASCADE,"
          "  FOREIGN KEY (class_id)   REFERENCES classes(id)  ON DELETE CASCADE"
          ");"
        },
        { 7,
          "CREATE TABLE IF NOT EXISTS exams ("
          "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
          "  teacher_id INTEGER NOT NULL,"
          "  class_id   INTEGER NOT NULL,"
          "  subject    TEXT NOT NULL,"
          "  exam_date  TEXT NOT NULL,"
          "  exam_time  TEXT NOT NULL,"
          "  description TEXT,"
          "  FOREIGN KEY (teacher_id) REFERENCES teachers(id) ON DELETE CASCADE,"
          "  FOREIGN KEY (class_id)   REFERENCES classes(id)  ON DELETE CASCADE"
          ");"
        },
        { 8,
          "CREATE TABLE IF NOT EXISTS assignments ("
          "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
          "  teacher_id INTEGER NOT NULL,"
          "  class_id   INTEGER NOT NULL,"
          "  subject    TEXT NOT NULL,"
          "  title      TEXT NOT NULL,"
          "  description TEXT,"
          "  due_date   TEXT NOT NULL,"
          "  FOREIGN KEY (teacher_id) REFERENCES teachers(id) ON DELETE CASCADE,"
          "  FOREIGN KEY (class_id)   REFERENCES classes(id)  ON DELETE CASCADE"
          ");"
        },
        { 9,
          "CREATE TABLE IF NOT EXISTS notices ("
          "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
          "  author_id  INTEGER NOT NULL,"
          "  class_id   INTEGER,"   /* NULL = global */
          "  title      TEXT NOT NULL,"
          "  content    TEXT NOT NULL,"
          "  status     TEXT NOT NULL DEFAULT 'ACTIVE'"
          "    CHECK (status IN ('ACTIVE','ARCHIVED')),"
          "  created_at TEXT NOT NULL DEFAULT (datetime('now')),"
          "  FOREIGN KEY (author_id) REFERENCES users(id) ON DELETE CASCADE,"
          "  FOREIGN KEY (class_id)  REFERENCES classes(id) ON DELETE CASCADE"
          ");"
        },
        { 10,
          "CREATE TABLE IF NOT EXISTS messages ("
          "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
          "  sender_phone TEXT NOT NULL,"
          "  user_type TEXT,"
          "  content TEXT NOT NULL,"
          "  msg_type TEXT NOT NULL DEFAULT 'TEXT'"
          "    CHECK (msg_type IN ('TEXT','AUDIO','IMAGE','OTHER')),"
          "  status TEXT NOT NULL DEFAULT 'RECEIVED'"
          "    CHECK (status IN ('RECEIVED','PROCESSED','FAILED','IGNORED')),"
          "  created_at TEXT NOT NULL DEFAULT (datetime('now'))"
          ");"
        },
        { 11,
          "CREATE TABLE IF NOT EXISTS conversations ("
          "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
          "  phone TEXT NOT NULL UNIQUE,"
          "  state TEXT NOT NULL DEFAULT 'IDLE',"
          "  context_json TEXT,"
          "  updated_at TEXT NOT NULL DEFAULT (datetime('now'))"
          ");"
        },
        { 12,
          "CREATE INDEX IF NOT EXISTS idx_tasks_class_due ON tasks(class_id, due_date);"
          "CREATE INDEX IF NOT EXISTS idx_exams_class_date ON exams(class_id, exam_date);"
          "CREATE INDEX IF NOT EXISTS idx_notices_class ON notices(class_id, created_at);"
          "CREATE INDEX IF NOT EXISTS idx_students_class ON students(class_id);"
          "CREATE INDEX IF NOT EXISTS idx_messages_phone ON messages(sender_phone, created_at);"
        },
        { 13,
          "CREATE TABLE IF NOT EXISTS pending_tasks ("
          "  phone TEXT PRIMARY KEY,"
          "  teacher_id INTEGER NOT NULL,"
          "  class_id INTEGER NOT NULL,"
          "  subject TEXT NOT NULL,"
          "  title TEXT NOT NULL,"
          "  description TEXT NOT NULL DEFAULT '',"
          "  due_date TEXT NOT NULL,"
          "  updated_at TEXT NOT NULL DEFAULT (datetime('now')),"
          "  FOREIGN KEY (teacher_id) REFERENCES teachers(id) ON DELETE CASCADE,"
          "  FOREIGN KEY (class_id) REFERENCES classes(id) ON DELETE CASCADE"
          ");"
        },
        { 14,
          "ALTER TABLE users ADD COLUMN email TEXT;"
        },
        { 15,
          "CREATE TABLE IF NOT EXISTS anonymous_students ("
          " phone TEXT PRIMARY KEY, class_id INTEGER, updated_at TEXT NOT NULL DEFAULT (datetime('now')),"
          " FOREIGN KEY(class_id) REFERENCES classes(id) ON DELETE SET NULL"
          ");"
        },
        { 16,
          "ALTER TABLE anonymous_students ADD COLUMN pending_question TEXT;"
        },
        { 17,
          "CREATE TABLE IF NOT EXISTS pending_notices ("
          " phone TEXT PRIMARY KEY, title TEXT NOT NULL, content TEXT NOT NULL,"
          " updated_at TEXT NOT NULL DEFAULT (datetime('now'))"
          ");"
        },
        { 18,
          "ALTER TABLE students ADD COLUMN grade TEXT NOT NULL DEFAULT '';"
        },
        { 19,
          "CREATE TABLE IF NOT EXISTS whatsapp_inbound_events ("
          " event_id TEXT PRIMARY KEY, sender_phone TEXT NOT NULL,"
          " status TEXT NOT NULL DEFAULT 'RECEIVED' CHECK(status IN ('RECEIVED','PROCESSED','DELIVERY_FAILED')) ,"
          " received_at TEXT NOT NULL DEFAULT (datetime('now'))"
          ");"
        },
        { 20,
          "ALTER TABLE whatsapp_inbound_events ADD COLUMN reply TEXT NOT NULL DEFAULT '';"
        }
    };

    size_t total = sizeof migs / sizeof migs[0];
    for (size_t i = 0; i < total; i++) {
        if (migs[i].v <= cur) continue;
        if (db_begin() != SUCCESS) {
            log_error("Nao foi possivel iniciar migration #%d: %s",
                      migs[i].v, database_last_error());
            return ERROR_DATABASE;
        }
        log_info("Aplicando migration #%d", migs[i].v);
        char *errmsg = NULL;
        int rc = sqlite3_exec(database_get(), migs[i].sql, NULL, NULL, &errmsg);
        if (rc != SQLITE_OK) {
            log_error("Migration #%d falhou: %s", migs[i].v,
                      errmsg ? errmsg : database_last_error());
            sqlite3_free(errmsg);
            db_rollback();
            return ERROR_DATABASE;
        }
        if (set_version(migs[i].v) != SUCCESS) {
            log_error("Nao foi possivel registrar migration #%d: %s",
                      migs[i].v, database_last_error());
            db_rollback();
            return ERROR_DATABASE;
        }
        if (db_commit() != SUCCESS) {
            log_error("Nao foi possivel concluir migration #%d: %s",
                      migs[i].v, database_last_error());
            db_rollback();
            return ERROR_DATABASE;
        }
    }
    return SUCCESS;
}

/* ===================== API pública =============================== */

int database_init(const char *path)
{
    if (!path || !*path) return ERROR_INVALID_INPUT;
    pthread_once(&g_db_once, init_key);
    strncpy(g_path, path, sizeof g_path - 1);
    g_path[sizeof g_path - 1] = '\0';
#if !defined(_WIN32)
    /* Restringe também bancos existentes sem alterar seus dados. */
    (void)chmod(g_path, 0600);
#endif

    /* Abre uma conexão para o bootstrap (pode falhar). */
    sqlite3 *db = open_connection(g_path);
    if (!db) return ERROR_DATABASE;
    pthread_setspecific(g_db_key, db);

    int rc = run_migrations();
    if (rc != SUCCESS) {
        log_error("Falha em migrations");
        return rc;
    }
    g_initialized = 1;
    log_info("Banco inicializado em %s", g_path);
    return SUCCESS;
}

void database_shutdown(void)
{
    sqlite3 *db = (sqlite3 *)pthread_getspecific(g_db_key);
    if (db) { sqlite3_close(db); pthread_setspecific(g_db_key, NULL); }
    g_initialized = 0;
}

int db_prepare(const char *sql, sqlite3_stmt **out)
{
    if (!sql || !out) return ERROR_INVALID_INPUT;
    sqlite3 *db = database_get();
    if (!db) return ERROR_DATABASE;
    int rc = sqlite3_prepare_v2(db, sql, -1, out, NULL);
    if (rc != SQLITE_OK) {
        log_error("db_prepare falhou: %s | sql=%.80s",
                  sqlite3_errmsg(db), sql);
        return ERROR_DATABASE;
    }
    return SUCCESS;
}

int db_bind_text(sqlite3_stmt *s, int idx, const char *value)
{
    if (!s) return ERROR_INVALID_INPUT;
    if (!value) return sqlite3_bind_null(s, idx);
    /* SQLITE_TRANSIENT: sqlite faz cópia. */
    return sqlite3_bind_text(s, idx, value, -1, SQLITE_TRANSIENT) == SQLITE_OK
        ? SUCCESS : ERROR_DATABASE;
}

int db_bind_int(sqlite3_stmt *s, int idx, int value)
{
    return sqlite3_bind_int(s, idx, value) == SQLITE_OK ? SUCCESS : ERROR_DATABASE;
}

int db_bind_int64(sqlite3_stmt *s, int idx, long long value)
{
    return sqlite3_bind_int64(s, idx, value) == SQLITE_OK ? SUCCESS : ERROR_DATABASE;
}

int db_bind_null(sqlite3_stmt *s, int idx)
{
    return sqlite3_bind_null(s, idx) == SQLITE_OK ? SUCCESS : ERROR_DATABASE;
}

int db_exec(const char *sql)
{
    if (!sql) return ERROR_INVALID_INPUT;
    sqlite3 *db = database_get();
    if (!db) return ERROR_DATABASE;
    char *errmsg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        log_error("db_exec falhou: %s | sql=%.80s", errmsg, sql);
        sqlite3_free(errmsg);
        return ERROR_DATABASE;
    }
    return SUCCESS;
}

int db_begin(void)    { return db_exec("BEGIN TRANSACTION;"); }
int db_commit(void)   { return db_exec("COMMIT;"); }
int db_rollback(void) { return db_exec("ROLLBACK;"); }
