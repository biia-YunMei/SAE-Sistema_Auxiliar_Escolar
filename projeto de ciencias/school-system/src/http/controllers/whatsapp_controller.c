/* whatsapp_controller.c
 * Webhook de entrada do WhatsApp.
 *
 * Recebe o envelope compatível com o provedor configurado ou o formato JSON simplificado usado
 * localmente e delega o processamento ao módulo whatsapp.c.
 */
#include "http_server.h"
#include "auth.h"
#include "database.h"
#include "validation.h"
#include "classes.h"
#include "users.h"
#include "logger.h"
#include "config.h"
#include "whatsapp.h"
#include "anonymous_enrollment.h"
#include "controllers.h"
#include "json_util.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

#if defined(HAVE_OPENSSL_SHA)
#include <openssl/evp.h>
#include <openssl/hmac.h>
#endif

static int json_extract_string(const char *body, const char *key,
                               char *out, size_t cap)
{
    return json_object_get_string(body, key, out, cap);
}

/* Extrai o primeiro evento no formato compatível com o provedor sem depender de um
 * parser JSON externo. A varredura respeita strings e caracteres escapados. */
static int json_extract_provider_message(const char *body, char *from, size_t from_cap,
                                     char *event_id, size_t id_cap, char *text,
                                     size_t text_cap, char *type, size_t type_cap)
{
    const char *messages = strstr(body, "\"messages\"");
    if (!messages) return 0;
    const char *array = strchr(messages, '[');
    if (!array) return 0;
    const char *object = strchr(array + 1, '{');
    if (!object) return 0;
    const char *p = object;
    int depth = 0, in_string = 0, escaped = 0;
    for (; *p; ++p) {
        if (in_string) {
            if (escaped) escaped = 0;
            else if (*p == '\\') escaped = 1;
            else if (*p == '"') in_string = 0;
            continue;
        }
        if (*p == '"') in_string = 1;
        else if (*p == '{') ++depth;
        else if (*p == '}' && --depth == 0) break;
    }
    if (!*p || depth != 0) return 0;
    size_t len = (size_t)(p - object) + 1;
    char *message = malloc(len + 1);
    if (!message) return -1;
    memcpy(message, object, len); message[len] = '\0';
    int ok = json_extract_string(message, "from", from, from_cap);
    (void)json_extract_string(message, "id", event_id, id_cap);
    (void)json_extract_string(message, "type", type, type_cap);
    if (ok && strcmp(type, "text") == 0)
        ok = json_extract_string(message, "body", text, text_cap);
    else if (ok && strcmp(type, "image") == 0)
        (void)json_extract_string(message, "caption", text, text_cap);
    free(message);
    return ok;
}

static int require_coordinator(const HttpRequest *req, HttpResponse *res)
{
    AuthSession session;
    if (!req->auth) {
        (void)http_respond_error(res, 401, "missing authorization");
        return ERROR_UNAUTHORIZED;
    }
    if (auth_session_resolve(req->auth, &session) != SUCCESS)
    {
        (void)http_respond_error(res, 401, "invalid session");
        return ERROR_UNAUTHORIZED;
    }
    if (session.type != USER_TYPE_COORD && session.type != USER_TYPE_PROF)
    {
        (void)http_respond_error(res, 403, "forbidden");
        return ERROR_FORBIDDEN;
    }
    return SUCCESS;
}

static int query_value(const char *query, const char *key, char *out, size_t cap)
{
    size_t key_len = strlen(key);
    const char *p = query ? query : "";
    while (*p) {
        const char *end = strchr(p, '&');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len > key_len && strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            size_t value_len = len - key_len - 1;
            if (value_len >= cap) return 0;
            memcpy(out, p + key_len + 1, value_len);
            out[value_len] = '\0';
            return 1;
        }
        p = end ? end + 1 : p + len;
    }
    return 0;
}

static int normalize_phone(const char *source, char *phone, size_t cap)
{
    if (!source || !*source || cap < 3) return 0;
    size_t used = 0;
    phone[used++] = '+';
    for (const unsigned char *p = (const unsigned char *)source; *p; ++p) {
        if (*p < '0' || *p > '9' || used + 1 >= cap) return 0;
        phone[used++] = (char)*p;
    }
    phone[used] = '\0';
    return validation_is_valid_phone(phone);
}

/* Evento identificado pelo provedor só executa o fluxo de negócio uma vez. */
static int claim_inbound_event(const char *event_id, const char *phone)
{
    if (!event_id || !*event_id || strlen(event_id) > 200 || !phone || !*phone)
        return ERROR_INVALID_INPUT;
    sqlite3_stmt *statement = NULL;
    if (db_prepare("INSERT OR IGNORE INTO whatsapp_inbound_events(event_id,sender_phone) VALUES(?,?);",
                   &statement) != SUCCESS) return ERROR_DATABASE;
    int bound = db_bind_text(statement, 1, event_id) == SUCCESS &&
                db_bind_text(statement, 2, phone) == SUCCESS;
    int step = bound ? sqlite3_step(statement) : SQLITE_ERROR;
    int inserted = step == SQLITE_DONE && sqlite3_changes(database_get()) == 1;
    sqlite3_finalize(statement);
    if (step != SQLITE_DONE) return ERROR_DATABASE;
    if (inserted) return 1;

    /* A crash may leave RECEIVED forever. Reclaim only after the longest
     * synchronous AI call plus a safety margin; the conditional update makes
     * concurrent provider retries safe. */
    if (db_prepare("UPDATE whatsapp_inbound_events SET received_at=datetime('now') "
                   "WHERE event_id=? AND sender_phone=? AND status='RECEIVED' "
                   "AND received_at<=datetime('now','-2 minutes');", &statement) != SUCCESS)
        return ERROR_DATABASE;
    bound = db_bind_text(statement, 1, event_id) == SUCCESS &&
            db_bind_text(statement, 2, phone) == SUCCESS;
    step = bound ? sqlite3_step(statement) : SQLITE_ERROR;
    int reclaimed = step == SQLITE_DONE && sqlite3_changes(database_get()) == 1;
    sqlite3_finalize(statement);
    if (step != SQLITE_DONE) return ERROR_DATABASE;
    return reclaimed ? 1 : 0;
}

static void finish_inbound_event(const char *event_id, const char *status,
                                 const char *reply)
{
    sqlite3_stmt *statement = NULL;
    if (db_prepare("UPDATE whatsapp_inbound_events SET status=?,reply=? WHERE event_id=?;", &statement) != SUCCESS)
        return;
    int bound = db_bind_text(statement, 1, status) == SUCCESS &&
                db_bind_text(statement, 2, reply ? reply : "") == SUCCESS &&
                db_bind_text(statement, 3, event_id) == SUCCESS;
    if (bound) (void)sqlite3_step(statement);
    sqlite3_finalize(statement);
}

static int failed_event_reply(const char *event_id, char *reply, size_t cap)
{
    sqlite3_stmt *statement = NULL;
    if (db_prepare("SELECT reply FROM whatsapp_inbound_events WHERE event_id=? AND status='DELIVERY_FAILED';",
                   &statement) != SUCCESS) return 0;
    db_bind_text(statement, 1, event_id);
    int found = sqlite3_step(statement) == SQLITE_ROW;
    if (found) {
        const unsigned char *stored = sqlite3_column_text(statement, 0);
        if (!stored || strlen((const char *)stored) >= cap) found = 0;
        else snprintf(reply, cap, "%s", (const char *)stored);
    }
    sqlite3_finalize(statement);
    return found;
}

static void release_inbound_event(const char *event_id)
{
    sqlite3_stmt *statement = NULL;
    if (db_prepare("DELETE FROM whatsapp_inbound_events WHERE event_id=? AND status='RECEIVED';",
                   &statement) != SUCCESS) return;
    db_bind_text(statement, 1, event_id);
    (void)sqlite3_step(statement);
    sqlite3_finalize(statement);
}

static int verify_webhook(const HttpRequest *req, HttpResponse *res)
{
    const AppConfig *cfg = config_current();
    char mode[32] = {0};
    char token[128] = {0};
    char challenge[512] = {0};
    if (!cfg || !cfg->whatsapp_verify_token[0]) {
        log_error("whatsapp: WHATSAPP_VERIFY_TOKEN não configurado");
        return http_respond_error(res, 500, "webhook not configured");
    }
    if (!query_value(req->query, "hub.mode", mode, sizeof mode) ||
        !query_value(req->query, "hub.verify_token", token, sizeof token) ||
        !query_value(req->query, "hub.challenge", challenge, sizeof challenge)) {
        return http_respond_error(res, 400, "invalid verification request");
    }
    if (strcmp(mode, "subscribe") != 0 || strcmp(token, cfg->whatsapp_verify_token) != 0) {
        log_warn("whatsapp: tentativa de verificação recusada");
        return http_respond_error(res, 403, "verification failed");
    }
    log_info("whatsapp: webhook validado pelo provedor configurado");
    return http_respond_text(res, 200, challenge);
}

static int verify_payload_signature(const HttpRequest *req, HttpResponse *res)
{
    const AppConfig *cfg = config_current();
    if (!cfg || !cfg->whatsapp_webhook_secret[0])
        return http_respond_error(res, 500, "webhook signature not configured");

#if !defined(HAVE_OPENSSL_SHA)
    log_error("whatsapp: assinatura configurada, mas OpenSSL nao esta disponivel");
    return http_respond_error(res, 500, "signature verification unavailable");
#else
    static const char hex[] = "0123456789abcdef";
    const char prefix[] = "sha256=";
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    char expected[sizeof prefix - 1 + EVP_MAX_MD_SIZE * 2 + 1];

    if (!req->webhook_signature ||
        strncmp(req->webhook_signature, prefix, sizeof prefix - 1) != 0) {
        return http_respond_error(res, 401, "missing webhook signature");
    }
    if (!HMAC(EVP_sha256(), cfg->whatsapp_webhook_secret,
              (int)strlen(cfg->whatsapp_webhook_secret),
              (const unsigned char *)req->body, req->body_len,
              digest, &digest_len) || digest_len != 32) {
        log_error("whatsapp: nao foi possivel calcular assinatura");
        return http_respond_error(res, 500, "signature verification failed");
    }
    memcpy(expected, prefix, sizeof prefix - 1);
    for (unsigned int i = 0; i < digest_len; ++i) {
        expected[sizeof prefix - 1 + i * 2] = hex[digest[i] >> 4];
        expected[sizeof prefix + i * 2] = hex[digest[i] & 0x0f];
    }
    expected[sizeof prefix - 1 + digest_len * 2] = '\0';

    size_t received_len = strlen(req->webhook_signature);
    size_t expected_len = strlen(expected);
    unsigned char diff = (unsigned char)(received_len ^ expected_len);
    for (size_t i = 0; i < expected_len; ++i) {
        unsigned char received = i < received_len
            ? (unsigned char)req->webhook_signature[i] : 0;
        diff |= (unsigned char)(received ^ (unsigned char)expected[i]);
    }
    if (diff != 0) {
        log_warn("whatsapp: assinatura de webhook invalida");
        return http_respond_error(res, 401, "invalid webhook signature");
    }
    return SUCCESS;
#endif
}

/* Endpoint de webhook compatível com o provedor configurado. */
int ctrl_whatsapp_webhook(const HttpRequest *req, HttpResponse *res)
{
    if (strcmp(req->method, "GET") == 0) return verify_webhook(req, res);

    if (strcmp(req->method, "POST") != 0)
        return http_respond_error(res, 405, "method not allowed");

    int signature_rc = verify_payload_signature(req, res);
    if (signature_rc != SUCCESS) return signature_rc;

    /* Eventos de status e mensagens que não são texto são confirmados,
     * mas não recebem uma resposta automática. */
    char raw_from[32] = {0}, from[32] = {0}, text[2048] = {0}, media_type[32] = {0};
    char event_id[201] = {0};
    int provider_event = json_extract_provider_message(req->body, raw_from, sizeof raw_from,
        event_id, sizeof event_id, text, sizeof text, media_type, sizeof media_type);
    if (provider_event < 0) return http_respond_error(res, 500, "out of memory");
    if (!provider_event && !json_extract_string(req->body, "from", raw_from, sizeof raw_from)) {
        return http_respond_json(res, 200, "{\"status\":\"ignored\"}");
    }
    if (!normalize_phone(raw_from, from, sizeof from))
        return http_respond_error(res, 400, "invalid phone");
    if (!event_id[0]) {
        (void)json_extract_string(req->body, "message_id", event_id, sizeof event_id);
    }
    /* Sem ID único não há deduplicação confiável; rejeitar evita efeitos
     * repetidos (incluindo tarefas) em retries do provedor. */
    if (!event_id[0]) return http_respond_error(res, 400, "message id required");
    if (event_id[0]) {
        int claimed = claim_inbound_event(event_id, from);
        if (claimed == 0) {
            char previous_reply[4096] = {0};
            if (failed_event_reply(event_id, previous_reply, sizeof previous_reply)) {
                int retry_rc = whatsapp_send_message(from, previous_reply);
                if (retry_rc == SUCCESS) {
                    finish_inbound_event(event_id, "PROCESSED", previous_reply);
                    return http_respond_json(res, 200, "{\"status\":\"delivery_retried\"}");
                }
                log_error("whatsapp: retentativa de entrega falhou para evento %s (%d)", event_id, retry_rc);
                return http_respond_json(res, 200, "{\"status\":\"delivery_failed\"}");
            }
            return http_respond_json(res, 200, "{\"status\":\"duplicate\"}");
        }
        if (claimed < 0) return http_respond_error(res, 500, "event persistence failed");
    }
    if ((!provider_event && !json_extract_string(req->body, "body", text, sizeof text)) ||
        (provider_event && strcmp(media_type, "text") != 0)) {
        if (provider_event && strcmp(media_type, "image") == 0 && text[0]) {
            /* Legendas de fotos seguem como texto contextual para a IA tutora. */
        } else {
            if (!provider_event) (void)json_extract_string(req->body, "type", media_type, sizeof media_type);
            const char *guidance = NULL;
            if (strcmp(media_type, "audio") == 0)
                guidance = "Ainda nao consigo transcrever audio. Envie a tarefa ou duvida por texto.";
            else if (strcmp(media_type, "image") == 0)
                guidance = "Recebi a imagem, mas ainda nao consigo analisar fotos. Descreva o exercicio por texto para eu orientar voce.";
            if (guidance) {
                int send_rc = whatsapp_send_message(from, guidance);
                if (send_rc != SUCCESS) {
                    if (event_id[0]) finish_inbound_event(event_id, "DELIVERY_FAILED", guidance);
                    return http_respond_json(res, 200, "{\"status\":\"delivery_failed\"}");
                }
                if (event_id[0]) finish_inbound_event(event_id, "PROCESSED", guidance);
                return http_respond_json(res, 200, "{\"status\":\"media_guidance_sent\"}");
            }
            if (event_id[0]) finish_inbound_event(event_id, "PROCESSED", "");
            return http_respond_json(res, 200, "{\"status\":\"ignored\"}");
        }
    }
    if (!validation_length_between(text, 1, 2000)) {
        if (event_id[0]) release_inbound_event(event_id);
        return http_respond_error(res, 400, "text too long");
    }

    char reply[2048];
    int rc = whatsapp_receive_message(from, text, reply, sizeof reply);
    if (rc != SUCCESS) {
        if (event_id[0]) release_inbound_event(event_id);
        return http_respond_error(res, 500, "processing failed");
    }

    rc = whatsapp_send_message(from, reply);
    if (rc != SUCCESS) {
        /* Confirmamos o evento para evitar que o provedor reenvie a mesma
         * mensagem e gere processamento duplicado. A falha fica registrada
         * no log para uma rotina de retentativa futura. */
        log_error("whatsapp: resposta para %s nao enviada (%d)", from, rc);
        if (event_id[0]) finish_inbound_event(event_id, "DELIVERY_FAILED", reply);
        return http_respond_json(res, 200, "{\"status\":\"delivery_failed\"}");
    }

    if (event_id[0]) finish_inbound_event(event_id, "PROCESSED", reply);

    return http_respond_json(res, 200, "{\"status\":\"processed\"}");
}

static int normalize_outgoing_phone(const char *source, char phone[32])
{
    if (!source || !*source) return 0;
    int has_country_prefix = source[0] == '+';
    char digits_text[24];
    size_t digits = 0;
    for (const unsigned char *p = (const unsigned char *)source; *p; ++p) {
        if (isdigit(*p)) {
            if (digits + 1 >= sizeof digits_text) return 0;
            digits_text[digits++] = (char)*p;
        } else if ((*p == '+' && p != (const unsigned char *)source) ||
                   (*p != '+' && *p != ' ' && *p != '-' && *p != '(' && *p != ')' && *p != '.')) {
            return 0;
        }
    }
    digits_text[digits] = '\0';
    if (digits < 8 || digits > 15) return 0;
    const char *country = (!has_country_prefix && (digits == 10 || digits == 11)) ? "55" : "";
    phone[0] = '+';
    size_t country_len = strlen(country);
    memcpy(phone + 1, country, country_len);
    memcpy(phone + 1 + country_len, digits_text, digits + 1);
    return validation_is_valid_phone(phone);
}

static int whatsapp_message_body(const HttpRequest *req, char phone[32],
                                char message[4001])
{
    char input_phone[32] = {0};
    if (!json_extract_string(req->body, "phone", input_phone, sizeof input_phone) ||
        !normalize_outgoing_phone(input_phone, phone) ||
        !json_extract_string(req->body, "message", message, 4001) ||
        !validation_length_between(message, 1, 4000)) return 0;
    for (const unsigned char *p = (const unsigned char *)message; *p; ++p)
        if (*p < 0x20 && *p != '\n' && *p != '\r' && *p != '\t') return 0;
    return 1;
}

/* Envio unitário pela API externa configurada no SAE. O destino precisa estar
 * cadastrado no sistema; a API externa da feira continua limitada a /reply. */
int ctrl_whatsapp_send(const HttpRequest *req, HttpResponse *res)
{
    if (strcmp(req->method, "POST") != 0)
        return http_respond_error(res, 405, "method not allowed");

    int auth_rc = require_coordinator(req, res);
    if (auth_rc != SUCCESS) return auth_rc;
    char phone[32] = {0}, message[4001] = {0};
    if (!whatsapp_message_body(req, phone, message))
        return http_respond_error(res, 400, "invalid phone or message");

    sqlite3_stmt *statement = NULL;
    if (db_prepare("SELECT 1 FROM users WHERE phone=? AND status='ACTIVE' LIMIT 1;",
                   &statement) != SUCCESS)
        return http_respond_error(res, 500, "database error");
    db_bind_text(statement, 1, phone);
    int registered = sqlite3_step(statement) == SQLITE_ROW;
    sqlite3_finalize(statement);
    if (!registered) return http_respond_error(res, 404, "registered contact not found");

    int rc = whatsapp_send_message(phone, message);
    if (rc == ERROR_CONFIG || rc == ERROR_NOT_IMPLEMENTED)
        return http_respond_error(res, 503, "WhatsApp provider is not configured");
    if (rc != SUCCESS) return http_respond_error(res, 502, "WhatsApp message could not be sent");
    return http_respond_json(res, 200, "{\"status\":\"sent\"}");
}

/* Pair an authenticated teacher account to the stable anonymous sender ref
 * used by the school inbox. The code is single-use, short-lived and returned
 * only to the authenticated teacher that requested it. */
int ctrl_whatsapp_teacher_link_code(const HttpRequest *req, HttpResponse *res)
{
    if (strcmp(req->method, "POST") != 0)
        return http_respond_error(res, 405, "method not allowed");
    AuthSession session;
    if (!req->auth || auth_session_resolve(req->auth, &session) != SUCCESS)
        return http_respond_error(res, 401, "invalid session");
    if (session.type != USER_TYPE_PROF)
        return http_respond_error(res, 403, "teacher only");
    char code[17];
    int rc = anonymous_teacher_link_create(session.user_id, code);
    if (rc == ERROR_CONFIG)
        return http_respond_error(res, 503, "secure code generation unavailable");
    if (rc == ERROR_FORBIDDEN)
        return http_respond_error(res, 403, "active teacher account required");
    if (rc != SUCCESS)
        return http_respond_error(res, 500, "could not create link code");
    char response[128];
    (void)snprintf(response, sizeof response,
        "{\"code\":\"%s\",\"expires_in_minutes\":10}", code);
    return http_respond_json(res, 201, response);
}

/* Remove o vínculo do WhatsApp apenas da conta de professor autenticada. */
int ctrl_whatsapp_teacher_unlink(const HttpRequest *req, HttpResponse *res)
{
    if (strcmp(req->method, "DELETE") != 0)
        return http_respond_error(res, 405, "method not allowed");
    AuthSession session;
    if (!req->auth || auth_session_resolve(req->auth, &session) != SUCCESS)
        return http_respond_error(res, 401, "invalid session");
    if (session.type != USER_TYPE_PROF)
        return http_respond_error(res, 403, "teacher only");

    int was_linked = 0;
    if (anonymous_teacher_link_remove(session.user_id, &was_linked) != SUCCESS)
        return http_respond_error(res, 500, "could not remove WhatsApp link");
    return http_respond_json(res, 200, was_linked
        ? "{\"status\":\"unlinked\",\"was_linked\":true}"
        : "{\"status\":\"unlinked\",\"was_linked\":false}");
}

/* Envio individual para estudantes matriculados na turma. Professores só
 * podem enviar para turmas às quais estão vinculados; coordenação pode todas. */
int ctrl_whatsapp_notify_class(const HttpRequest *req, HttpResponse *res)
{
    if (strcmp(req->method, "POST") != 0)
        return http_respond_error(res, 405, "method not allowed");
    AuthSession sender;
    if (!req->auth || auth_session_resolve(req->auth, &sender) != SUCCESS)
        return http_respond_error(res, 401, "invalid session");
    if (sender.type != USER_TYPE_COORD && sender.type != USER_TYPE_PROF)
        return http_respond_error(res, 403, "forbidden");

    char message[4001] = {0};
    int64_t class_id = 0;
    if (!json_object_get_int64(req->body, "class_id", &class_id)) class_id = 0;
    if (class_id <= 0 || !json_extract_string(req->body, "message", message, sizeof message) ||
        !validation_length_between(message, 1, 4000))
        return http_respond_error(res, 400, "class_id and message are required");
    for (const unsigned char *p = (const unsigned char *)message; *p; ++p)
        if (*p < 0x20 && *p != '\n' && *p != '\r' && *p != '\t')
            return http_respond_error(res, 400, "message contains invalid characters");

    if (sender.type == USER_TYPE_PROF) {
        sqlite3_stmt *teacher = NULL;
        if (db_prepare("SELECT id FROM teachers WHERE user_id=?;", &teacher) != SUCCESS)
            return http_respond_error(res, 500, "database error");
        db_bind_int64(teacher, 1, sender.user_id);
        int found = sqlite3_step(teacher) == SQLITE_ROW;
        long long teacher_id = found ? sqlite3_column_int64(teacher, 0) : 0;
        sqlite3_finalize(teacher);
        if (!found || !classes_teacher_has_access(teacher_id, class_id))
            return http_respond_error(res, 403, "not linked to class");
    }

    sqlite3_stmt *statement = NULL;
    if (db_prepare(
            "SELECT u.phone FROM students s JOIN users u ON u.id=s.user_id "
            "JOIN classes c ON c.id=s.class_id "
            "WHERE s.class_id=? AND c.status='ACTIVE' AND u.status='ACTIVE' "
            "ORDER BY s.id LIMIT 101;", &statement) != SUCCESS)
        return http_respond_error(res, 500, "database error");
    db_bind_int64(statement, 1, class_id);
    char (*phones)[32] = calloc(101, sizeof *phones);
    if (!phones) { sqlite3_finalize(statement); return http_respond_error(res, 500, "out of memory"); }
    size_t count = 0;
    int step;
    while ((step = sqlite3_step(statement)) == SQLITE_ROW && count < 101) {
        const unsigned char *value = sqlite3_column_text(statement, 0);
        if (!value || !normalize_outgoing_phone((const char *)value, phones[count])) continue;
        ++count;
    }
    sqlite3_finalize(statement);
    if (count > 100) { free(phones); return http_respond_error(res, 413, "class exceeds 100 recipients"); }
    if (step != SQLITE_DONE) { free(phones); return http_respond_error(res, 500, "database error"); }
    if (count == 0) { free(phones); return http_respond_error(res, 404, "class has no registered students"); }

    size_t sent = 0, failed = 0;
    for (size_t i = 0; i < count; ++i) {
        if (whatsapp_send_message(phones[i], message) == SUCCESS) ++sent;
        else ++failed;
    }
    free(phones);
    char response[128];
    (void)snprintf(response, sizeof response,
                   "{\"status\":\"%s\",\"sent\":%zu,\"failed\":%zu}",
                   failed ? "partial" : "sent", sent, failed);
    if (sent == 0) return http_respond_error(res, 502, "WhatsApp messages could not be sent");
    return http_respond_json(res, failed ? 207 : 200, response);
}

/* Health check — útil em Docker/Kubernetes. */
int ctrl_health(const HttpRequest *req, HttpResponse *res)
{
    (void)req;
    sqlite3_stmt *statement = NULL;
    if (db_prepare("SELECT 1;", &statement) != SUCCESS)
        return http_respond_error(res, 503, "database unavailable");
    int ready = sqlite3_step(statement) == SQLITE_ROW;
    sqlite3_finalize(statement);
    if (!ready) return http_respond_error(res, 503, "database unavailable");
    return http_respond_json(res, 200,
        "{\"status\":\"ok\",\"ready\":true,\"checks\":{\"database\":true}}");
}
