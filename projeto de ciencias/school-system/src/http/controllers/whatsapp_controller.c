/* whatsapp_controller.c
 * Webhook de entrada do WhatsApp.
 *
 * Recebe o envelope oficial da Meta ou o formato JSON simplificado usado
 * localmente e delega o processamento ao módulo whatsapp.c.
 */
#include "http_server.h"
#include "auth.h"
#include "database.h"
#include "validation.h"
#include "logger.h"
#include "config.h"
#include "whatsapp.h"
#include "controllers.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if defined(HAVE_OPENSSL_SHA)
#include <openssl/evp.h>
#include <openssl/hmac.h>
#endif

static int json_extract_string(const char *body, const char *key,
                               char *out, size_t cap)
{
    char needle[64];
    int written = snprintf(needle, sizeof needle, "\"%s\"", key);
    if (written <= 0 || (size_t)written >= sizeof needle) return 0;
    const char *p = strstr(body, needle);
    if (!p) return 0;
    p += written;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p++ != ':') return 0;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p++ != '"') return 0;

    size_t used = 0;
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) {
            ++p;
            if (*p == 'n') out[used++] = '\n';
            else if (*p == 'r') out[used++] = '\r';
            else if (*p == 't') out[used++] = '\t';
            else out[used++] = *p;
            ++p;
        } else {
            if (used + 1 >= cap) return 0;
            out[used++] = *p++;
        }
        if (used + 1 >= cap) return 0;
    }
    if (*p != '"') return 0;
    out[used] = '\0';
    return 1;
}

/* Extrai o primeiro evento no formato oficial da Meta sem depender de um
 * parser JSON externo. A varredura respeita strings e caracteres escapados. */
static int json_extract_meta_message(const char *body, char *from, size_t from_cap,
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

static long long json_extract_int(const char *body, const char *key)
{
    char needle[64];
    int written = snprintf(needle, sizeof needle, "\"%s\":", key);
    const char *value = body ? strstr(body, needle) : NULL;
    if (written < 0 || !value) return 0;
    value += written;
    return strtoll(value, NULL, 10);
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

/* Evento com ID da Meta só pode executar o fluxo de negócio uma vez. */
static int claim_inbound_event(const char *event_id, const char *phone)
{
    sqlite3_stmt *statement = NULL;
    if (db_prepare("INSERT OR IGNORE INTO whatsapp_inbound_events(event_id,sender_phone) VALUES(?,?);",
                   &statement) != SUCCESS) return ERROR_DATABASE;
    db_bind_text(statement, 1, event_id);
    db_bind_text(statement, 2, phone);
    int step = sqlite3_step(statement);
    int inserted = step == SQLITE_DONE && sqlite3_changes(database_get()) == 1;
    sqlite3_finalize(statement);
    return step == SQLITE_DONE ? (inserted ? 1 : 0) : ERROR_DATABASE;
}

static void finish_inbound_event(const char *event_id, const char *status,
                                 const char *reply)
{
    sqlite3_stmt *statement = NULL;
    if (db_prepare("UPDATE whatsapp_inbound_events SET status=?,reply=? WHERE event_id=?;", &statement) != SUCCESS)
        return;
    db_bind_text(statement, 1, status);
    db_bind_text(statement, 2, reply ? reply : "");
    db_bind_text(statement, 3, event_id);
    (void)sqlite3_step(statement);
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
    log_info("whatsapp: webhook verificado pela Meta");
    return http_respond_text(res, 200, challenge);
}

static int verify_payload_signature(const HttpRequest *req, HttpResponse *res)
{
    const AppConfig *cfg = config_current();
    if (!cfg || !cfg->whatsapp_app_secret[0])
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
    if (!HMAC(EVP_sha256(), cfg->whatsapp_app_secret,
              (int)strlen(cfg->whatsapp_app_secret),
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

/* Webhook oficial da Meta. GET confirma o callback; POST recebe eventos. */
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
    int meta_event = json_extract_meta_message(req->body, raw_from, sizeof raw_from,
        event_id, sizeof event_id, text, sizeof text, media_type, sizeof media_type);
    if (meta_event < 0) return http_respond_error(res, 500, "out of memory");
    if (!meta_event && !json_extract_string(req->body, "from", raw_from, sizeof raw_from)) {
        return http_respond_json(res, 200, "{\"status\":\"ignored\"}");
    }
    if (!normalize_phone(raw_from, from, sizeof from))
        return http_respond_error(res, 400, "invalid phone");
    if (!event_id[0]) {
        (void)(json_extract_string(req->body, "id", event_id, sizeof event_id) ||
               json_extract_string(req->body, "message_id", event_id, sizeof event_id));
    }
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
    if ((!meta_event && !json_extract_string(req->body, "body", text, sizeof text)) ||
        (meta_event && strcmp(media_type, "text") != 0)) {
        if (meta_event && strcmp(media_type, "image") == 0 && text[0]) {
            /* Legendas de fotos seguem como texto contextual para a IA tutora. */
        } else {
            if (!meta_event) (void)json_extract_string(req->body, "type", media_type, sizeof media_type);
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
        /* Confirmamos o evento para evitar que a Meta reenvie a mesma
         * mensagem e gere processamento duplicado. A falha fica registrada
         * no log para uma rotina de retentativa futura. */
        log_error("whatsapp: resposta para %s nao enviada (%d)", from, rc);
        if (event_id[0]) finish_inbound_event(event_id, "DELIVERY_FAILED", reply);
        return http_respond_json(res, 200, "{\"status\":\"delivery_failed\"}");
    }

    if (event_id[0]) finish_inbound_event(event_id, "PROCESSED", reply);

    return http_respond_json(res, 200, "{\"status\":\"processed\"}");
}

/* Envio manual de aviso. Mantemos a credencial da Meta exclusivamente no
 * servidor; o cliente CLI apenas usa a sessao autenticada do coordenador. */
int ctrl_whatsapp_send(const HttpRequest *req, HttpResponse *res)
{
    if (strcmp(req->method, "POST") != 0)
        return http_respond_error(res, 405, "method not allowed");

    int auth_rc = require_coordinator(req, res);
    if (auth_rc != SUCCESS) return auth_rc;

    char phone[32] = {0};
    char message[4001] = {0};
    if (!json_extract_string(req->body, "phone", phone, sizeof phone) ||
        !json_extract_string(req->body, "message", message, sizeof message)) {
        return http_respond_error(res, 400, "phone and message are required");
    }
    if (!validation_is_valid_phone(phone))
        return http_respond_error(res, 400, "invalid phone");
    if (!validation_length_between(message, 1, 4000) ||
        !validation_is_safe_text(message)) {
        return http_respond_error(res, 400, "invalid message");
    }

    int send_rc = whatsapp_send_message(phone, message);
    if (send_rc != SUCCESS) {
        log_error("whatsapp: notificacao manual falhou para %s (%d)", phone, send_rc);
        return http_respond_error(res, 502, "notification delivery failed");
    }
    log_info("whatsapp: notificacao manual enviada para %s", phone);
    return http_respond_json(res, 200, "{\"status\":\"sent\"}");
}

/* Envia o mesmo comunicado a todos os alunos ativos de uma turma. A Meta
 * pode recusar envios fora da janela permitida; nesse caso a resposta mostra
 * quantos foram enviados e quantos falharam, sem interromper o lote. */
int ctrl_whatsapp_notify_class(const HttpRequest *req, HttpResponse *res)
{
    if (strcmp(req->method, "POST") != 0)
        return http_respond_error(res, 405, "method not allowed");
    int auth_rc = require_coordinator(req, res);
    if (auth_rc != SUCCESS) return auth_rc;

    long long class_id = json_extract_int(req->body, "class_id");
    char message[4001] = {0};
    if (class_id <= 0 || !json_extract_string(req->body, "message", message, sizeof message))
        return http_respond_error(res, 400, "class_id and message are required");
    if (!validation_length_between(message, 1, 4000) || !validation_is_safe_text(message))
        return http_respond_error(res, 400, "invalid message");

    sqlite3_stmt *statement = NULL;
    if (db_prepare("SELECT u.phone FROM students s JOIN users u ON u.id=s.user_id "
                   "WHERE s.class_id=? AND u.status='ACTIVE';", &statement) != SUCCESS)
        return http_respond_error(res, 500, "database error");
    db_bind_int64(statement, 1, class_id);
    int sent = 0;
    int failed = 0;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const char *phone = (const char *)sqlite3_column_text(statement, 0);
        if (phone && whatsapp_send_message(phone, message) == SUCCESS) ++sent;
        else ++failed;
    }
    sqlite3_finalize(statement);

    char response[128];
    snprintf(response, sizeof response, "{\"sent\":%d,\"failed\":%d}", sent, failed);
    log_info("whatsapp: aviso de turma=%lld enviados=%d falhas=%d",
             class_id, sent, failed);
    return http_respond_json(res, 200, response);
}

/* Health check — útil em Docker/Kubernetes. */
int ctrl_health(const HttpRequest *req, HttpResponse *res)
{
    (void)req;
    return http_respond_json(res, 200, "{\"status\":\"ok\"}");
}
