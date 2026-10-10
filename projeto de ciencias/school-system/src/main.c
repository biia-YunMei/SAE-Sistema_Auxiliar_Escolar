/* main.c
 * Bootstrap do back-end:
 *   - config
 *   - logger
 *   - database
 *   - whatsapp (init)
 *   - HTTP server
 *
 * Regra #29: nenhuma regra de negócio, SQL ou HTTP vive aqui.
 */
#include "config.h"
#include "auth.h"
#include "error.h"
#include "logger.h"
#include "database.h"
#include "http_server.h"
#include "router.h"
#include "controllers.h"
#include "whatsapp.h"
#include "cli.h"
#include "escola_api.h"

#include <stdio.h>
#include <stdlib.h>
#if !defined(_WIN32)
#include <sys/stat.h>
#endif
#include <signal.h>
#include <string.h>
#if defined(_WIN32)
  #define strcasecmp _stricmp
#else
  #include <strings.h>
#endif

#if defined(_WIN32)
  #include <windows.h>
#endif

static volatile sig_atomic_t g_should_exit = 0;
static void on_signal(int s) { (void)s; g_should_exit = 1; }

static LogLevel parse_level(const char *s)
{
    if (!s) return LOG_INFO;
    if (strcasecmp(s, "DEBUG") == 0) return LOG_DEBUG;
    if (strcasecmp(s, "WARN")  == 0 || strcasecmp(s, "WARNING") == 0) return LOG_WARN;
    if (strcasecmp(s, "ERROR") == 0) return LOG_ERROR;
    return LOG_INFO;
}

/* Tabela de rotas — único ponto que conhece os controllers. */
static const Route g_routes[] = {
    { "GET",  "/api/health",                       ctrl_health             },
    { "POST", "/api/auth",                         ctrl_auth_login         },
    { "POST", "/api/auth/logout",                  ctrl_auth_logout        },
    { "POST", "/api/bootstrap",                    ctrl_bootstrap_admin    },
    { "GET",  "/api/notices",                      ctrl_notices_list       },
    { "POST", "/api/notices",                      ctrl_notices_create     },
    { "PUT",  "/api/notices/:id",                  wr_notices_update       },
    { "DELETE", "/api/notices/:id",                wr_notices_delete       },
    { "POST", "/api/users",                        ctrl_users_create       },
    { "GET",  "/api/users",                        ctrl_users_list         },
    { "GET",  "/api/teachers",                     ctrl_teachers_list      },
    { "POST", "/api/teachers",                     ctrl_teachers_create    },
    { "DELETE", "/api/teachers/:id",               wr_teachers_delete      },
    { "PUT",  "/api/users/:id",                    wr_users_update         },
    { "DELETE", "/api/users/:id",                  wr_users_delete         },
    { "POST", "/api/students/class",               ctrl_students_assign_class },
    { "POST", "/api/students",                    ctrl_students_create    },
    { "GET",  "/api/students",                    ctrl_students_list      },

    { "GET",  "/api/classes",                      ctrl_classes_list       },
    { "POST", "/api/classes",                      ctrl_classes_create     },
    { "PUT",  "/api/classes/:id",                  wr_classes_update       },
    { "DELETE","/api/classes/:id",                 wr_classes_delete       },
    { "POST", "/api/classes/:id/teachers",         wr_classes_add_teacher  },
    { "DELETE", "/api/classes/:id/teachers/:tid",  wr_classes_remove_teacher },

    { "GET",  "/api/tasks",                        ctrl_tasks_list         },
    { "POST", "/api/tasks",                        ctrl_tasks_create       },
    { "PUT",  "/api/tasks/:id",                    wr_tasks_update         },
    { "DELETE","/api/tasks/:id",                   wr_tasks_delete         },

    { "GET",  "/api/whatsapp/webhook",             ctrl_whatsapp_webhook   },
    { "POST", "/api/whatsapp/webhook",             ctrl_whatsapp_webhook   },
    { "POST", "/api/whatsapp/messages",            ctrl_whatsapp_send      },
    { "POST", "/api/whatsapp/notify-class",        ctrl_whatsapp_notify_class }
};

struct ServerRoutes { const Route *routes; size_t n; };

int main(int argc, char **argv)
{
#if !defined(_WIN32)
    /* Tokens, banco, WAL e logs recém-criados ficam privados ao usuário. */
    (void)umask(0077);
#endif
    int cli_status = cli_run(argc, argv);
    if (cli_status >= 0) return cli_status;

    /* "sae servidor" e a forma antiga "sae" iniciam a API. */
    if (argc == 2 && strcmp(argv[1], "servidor") != 0) return ERROR_INVALID_INPUT;
    AppConfig cfg;
    int rc = config_load(&cfg);
    if (rc != SUCCESS) {
        fprintf(stderr, "Falha ao carregar configuração (código %d)\n", rc);
        return rc ? rc : 1;
    }

    logger_init(config_get_log_dir(&cfg), parse_level(config_get_log_level(&cfg)));
    auth_set_session_timeout(cfg.session_timeout_min);
    log_info("=== School Backend iniciando ===");
    log_info("Servidor %s:%d | Banco: %s",
             cfg.server_host, cfg.server_port, config_get_database_path(&cfg));

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    rc = database_init(cfg.database_path);
    if (rc != SUCCESS) {
        log_error("Falha ao inicializar banco");
        return rc;
    }

    if (whatsapp_init() != SUCCESS) {
        log_error("Falha ao inicializar módulo WhatsApp");
        return ERROR_WHATSAPP;
    }

    if (escola_api_worker_start((volatile int *)&g_should_exit) != SUCCESS)
        log_warn("Não foi possível iniciar a tutoria automática da inbox escolar");

    struct ServerRoutes sr = {
        .routes = g_routes,
        .n      = sizeof g_routes / sizeof g_routes[0]
    };

    log_info("Rotas registradas: %zu", sr.n);
    rc = http_server_run(cfg.server_host, cfg.server_port, &sr,
                         (volatile int *)&g_should_exit);
    g_should_exit = 1;
    escola_api_worker_join();
    if (rc != SUCCESS) {
        log_error("Servidor HTTP encerrou com erro (%d)", rc);
        whatsapp_shutdown();
        database_shutdown();
        logger_shutdown();
        return rc;
    }

    log_info("=== School Backend finalizando ===");
    whatsapp_shutdown();
    database_shutdown();
    logger_shutdown();
    return SUCCESS;
}
