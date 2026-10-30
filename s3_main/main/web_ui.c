#include "web_ui.h"
#include "s3_ble_scanner.h"
#include "s3_wifi_owner.h"

#include "c5_spi_link.h"
#include "privacy.h"
#include "recovery.h"
#include "qr_renderer.h"
#include "settings.h"
#include "storage.h"
#include "system_health.h"

#include "bootloader_random.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "nvs.h"
#include "psa/crypto.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WEB_NAMESPACE "webui"
#define WEB_PASSWORD_SALT_BYTES 16U
#define WEB_PASSWORD_HASH_BYTES 32U
#define WEB_PBKDF2_ITERATIONS 60000U
#define WEB_SESSION_COUNT 3U
#define WEB_SESSION_TOKEN_BYTES 24U
#define WEB_CSRF_TOKEN_BYTES 16U
#define WEB_SESSION_LIFETIME_US (15LL * 60LL * 1000000LL)
#define WEB_BODY_MAX 512U
#define WEB_LOGIN_FAILURE_LIMIT 5U
#define WEB_LOGIN_LOCKOUT_US (30LL * 1000000LL)

typedef struct {
    bool active;
    uint8_t token[WEB_SESSION_TOKEN_BYTES];
    uint8_t csrf[WEB_CSRF_TOKEN_BYTES];
    int64_t expires_us;
} web_session_t;

static const char *TAG = "web_ui";
static httpd_handle_t web_server;
static esp_netif_t *web_netif;
static bool web_wifi_claimed;
static bool web_wifi_initialized;
static bool web_wifi_started;
static bool web_handler_registered;
static esp_event_handler_instance_t web_wifi_handler;
static cerberus_web_ui_status_t web_status;
static web_session_t web_sessions[WEB_SESSION_COUNT];
static uint8_t password_salt[WEB_PASSWORD_SALT_BYTES];
static uint8_t password_hash[WEB_PASSWORD_HASH_BYTES];
static uint8_t login_failures;
static int64_t login_locked_until_us;
static uint8_t web_qr_modules[CERBERUS_QR_MAX_BITMAP_BYTES];

static const char web_style[] =
    "body{font-family:system-ui;background:#080e18;color:#edf6ff;margin:0}"
    "header{background:#111e2b;padding:18px;border-bottom:2px solid #43ddec}"
    "main{max-width:980px;margin:auto;padding:18px}.card{background:#111e2b;border:1px solid #293e55;border-radius:12px;padding:16px;margin:12px 0}"
    "input,select,button{font:inherit;padding:10px;margin:5px;background:#142638;color:#edf6ff;border:1px solid #43ddec;border-radius:6px}"
    "button{cursor:pointer}table{width:100%;border-collapse:collapse}td,th{padding:7px;border-bottom:1px solid #293e55;text-align:left}"
    "a{color:#43ddec}.bad{color:#ffbb64}.good{color:#83eab0}code{word-break:break-all}";

static void random_bytes(uint8_t *output, size_t length)
{
    while (length > 0U) {
        uint32_t value = esp_random();
        size_t count = length < sizeof(value) ? length : sizeof(value);
        memcpy(output, &value, count);
        output += count;
        length -= count;
    }
}

static void hex_encode(const uint8_t *input, size_t length, char *output)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t index = 0U; index < length; ++index) {
        output[index * 2U] = hex[input[index] >> 4U];
        output[index * 2U + 1U] = hex[input[index] & 0x0FU];
    }
    output[length * 2U] = '\0';
}

static bool constant_time_equal(const uint8_t *left, const uint8_t *right, size_t length)
{
    uint8_t difference = 0U;
    for (size_t index = 0U; index < length; ++index) {
        difference |= left[index] ^ right[index];
    }
    return difference == 0U;
}

static void html_escape(const char *input, char *output, size_t capacity)
{
    size_t used = 0U;
    while (*input != '\0' && used + 1U < capacity) {
        const char *replacement = NULL;
        if (*input == '&') replacement = "&amp;";
        else if (*input == '<') replacement = "&lt;";
        else if (*input == '>') replacement = "&gt;";
        else if (*input == '"') replacement = "&quot;";
        else if (*input == '\'') replacement = "&#39;";
        if (replacement != NULL) {
            size_t length = strlen(replacement);
            if (used + length >= capacity) break;
            memcpy(output + used, replacement, length);
            used += length;
        } else {
            output[used++] = (unsigned char)*input >= 32U ? *input : '?';
        }
        ++input;
    }
    output[used] = '\0';
}

static void url_encode(const char *input, char *output, size_t capacity)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t used = 0U;
    while (*input != '\0' && used + 1U < capacity) {
        unsigned char value = (unsigned char)*input++;
        bool plain = isalnum(value) || value == '-' || value == '_' || value == '.' || value == '~' || value == '/';
        if (plain) {
            output[used++] = (char)value;
        } else {
            if (used + 3U >= capacity) break;
            output[used++] = '%'; output[used++] = hex[value >> 4U]; output[used++] = hex[value & 0x0FU];
        }
    }
    output[used] = '\0';
}

static esp_err_t derive_password(const char *password, const uint8_t salt[WEB_PASSWORD_SALT_BYTES], uint8_t output[WEB_PASSWORD_HASH_BYTES])
{
    psa_key_derivation_operation_t operation = PSA_KEY_DERIVATION_OPERATION_INIT;
    psa_status_t status = psa_crypto_init();
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_setup(&operation, PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA_256));
    }
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_input_integer(&operation, PSA_KEY_DERIVATION_INPUT_COST, WEB_PBKDF2_ITERATIONS);
    }
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SALT, salt, WEB_PASSWORD_SALT_BYTES);
    }
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_PASSWORD,
            (const uint8_t *)password, strlen(password));
    }
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_output_bytes(&operation, output, WEB_PASSWORD_HASH_BYTES);
    }
    (void)psa_key_derivation_abort(&operation);
    return status == PSA_SUCCESS ? ESP_OK : ESP_FAIL;
}

static esp_err_t load_or_create_configuration(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WEB_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    size_t salt_length = sizeof(password_salt);
    size_t hash_length = sizeof(password_hash);
    bool configured = nvs_get_blob(handle, "salt", password_salt, &salt_length) == ESP_OK &&
        salt_length == sizeof(password_salt) &&
        nvs_get_blob(handle, "hash", password_hash, &hash_length) == ESP_OK &&
        hash_length == sizeof(password_hash);
    web_status.password_configured = configured;

    size_t password_length = sizeof(web_status.access_point_password);
    err = nvs_get_str(handle, "ap_pass", web_status.access_point_password, &password_length);
    bool replace_password = err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NVS_INVALID_LENGTH ||
        (err == ESP_OK && (password_length < 13U || password_length > 64U ||
            strcmp(web_status.access_point_password, "C3rb3rus") == 0));
    if (replace_password) {
        uint8_t seed[12];
        random_bytes(seed, sizeof(seed));
        hex_encode(seed, sizeof(seed), web_status.access_point_password);
        memset(seed, 0, sizeof(seed));
        err = nvs_set_str(handle, "ap_pass", web_status.access_point_password);
        if (err == ESP_OK) err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static esp_err_t save_password(const char *password)
{
    if (password == NULL || strlen(password) < 12U || strlen(password) > 96U) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t new_salt[WEB_PASSWORD_SALT_BYTES];
    uint8_t new_hash[WEB_PASSWORD_HASH_BYTES];
    random_bytes(new_salt, sizeof(new_salt));
    ESP_RETURN_ON_ERROR(derive_password(password, new_salt, new_hash), TAG, "password derivation failed");
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(WEB_NAMESPACE, NVS_READWRITE, &handle), TAG, "web NVS open failed");
    esp_err_t err = nvs_set_blob(handle, "salt", new_salt, sizeof(new_salt));
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, "hash", new_hash, sizeof(new_hash));
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err == ESP_OK) {
        memcpy(password_salt, new_salt, sizeof(password_salt));
        memcpy(password_hash, new_hash, sizeof(password_hash));
        web_status.password_configured = true;
    }
    memset(new_hash, 0, sizeof(new_hash));
    return err;
}

static bool password_matches(const char *password)
{
    uint8_t candidate[WEB_PASSWORD_HASH_BYTES];
    if (!web_status.password_configured || derive_password(password, password_salt, candidate) != ESP_OK) {
        return false;
    }
    bool matches = constant_time_equal(candidate, password_hash, sizeof(candidate));
    memset(candidate, 0, sizeof(candidate));
    return matches;
}

static esp_err_t read_body(httpd_req_t *request, char *body, size_t capacity)
{
    if (request->content_len <= 0 || (size_t)request->content_len >= capacity) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t received = 0U;
    while (received < (size_t)request->content_len) {
        int count = httpd_req_recv(request, body + received, request->content_len - received);
        if (count <= 0) {
            return ESP_FAIL;
        }
        received += (size_t)count;
    }
    body[received] = '\0';
    return ESP_OK;
}

static void url_decode(char *value)
{
    char *output = value;
    for (char *input = value; *input != '\0'; ++input) {
        if (*input == '+' ) {
            *output++ = ' ';
        } else if (*input == '%' && isxdigit((unsigned char)input[1]) && isxdigit((unsigned char)input[2])) {
            char digits[3] = {input[1], input[2], '\0'};
            *output++ = (char)strtoul(digits, NULL, 16);
            input += 2;
        } else {
            *output++ = *input;
        }
    }
    *output = '\0';
}

static bool form_value(const char *body, const char *key, char *output, size_t capacity)
{
    size_t key_length = strlen(key);
    const char *cursor = body;
    while (cursor != NULL && *cursor != '\0') {
        if (strncmp(cursor, key, key_length) == 0 && cursor[key_length] == '=') {
            cursor += key_length + 1U;
            const char *end = strchr(cursor, '&');
            size_t length = end != NULL ? (size_t)(end - cursor) : strlen(cursor);
            if (length >= capacity) {
                return false;
            }
            memcpy(output, cursor, length);
            output[length] = '\0';
            url_decode(output);
            return true;
        }
        cursor = strchr(cursor, '&');
        if (cursor != NULL) {
            ++cursor;
        }
    }
    return false;
}

static web_session_t *create_session(void)
{
    int64_t now = esp_timer_get_time();
    web_session_t *session = &web_sessions[0];
    for (size_t index = 0U; index < WEB_SESSION_COUNT; ++index) {
        if (!web_sessions[index].active || web_sessions[index].expires_us <= now) {
            session = &web_sessions[index];
            break;
        }
        if (web_sessions[index].expires_us < session->expires_us) {
            session = &web_sessions[index];
        }
    }
    random_bytes(session->token, sizeof(session->token));
    random_bytes(session->csrf, sizeof(session->csrf));
    session->active = true;
    session->expires_us = now + WEB_SESSION_LIFETIME_US;
    return session;
}

static web_session_t *authenticated_session(httpd_req_t *request)
{
    char cookie[160];
    if (httpd_req_get_hdr_value_str(request, "Cookie", cookie, sizeof(cookie)) != ESP_OK) {
        return NULL;
    }
    const char *token = strstr(cookie, "CERBERUS_SESSION=");
    if (token == NULL) {
        return NULL;
    }
    token += strlen("CERBERUS_SESSION=");
    char expected[WEB_SESSION_TOKEN_BYTES * 2U + 1U];
    int64_t now = esp_timer_get_time();
    for (size_t index = 0U; index < WEB_SESSION_COUNT; ++index) {
        web_session_t *session = &web_sessions[index];
        if (!session->active || session->expires_us <= now) {
            session->active = false;
            continue;
        }
        hex_encode(session->token, sizeof(session->token), expected);
        if (strncmp(token, expected, sizeof(expected) - 1U) == 0) {
            session->expires_us = now + WEB_SESSION_LIFETIME_US;
            return session;
        }
    }
    return NULL;
}

static bool csrf_valid(httpd_req_t *request, web_session_t *session)
{
    char supplied[WEB_CSRF_TOKEN_BYTES * 2U + 1U];
    char expected[sizeof(supplied)];
    if (session == NULL || httpd_req_get_hdr_value_str(request, "X-CSRF-Token", supplied, sizeof(supplied)) != ESP_OK) {
        return false;
    }
    hex_encode(session->csrf, sizeof(session->csrf), expected);
    return constant_time_equal((const uint8_t *)supplied, (const uint8_t *)expected, sizeof(expected) - 1U);
}

static esp_err_t send_security_headers(httpd_req_t *request)
{
    ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(request, "Cache-Control", "no-store"), TAG, "cache header failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff"), TAG, "content header failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(request, "X-Frame-Options", "DENY"), TAG, "frame header failed");
    return httpd_resp_set_hdr(request, "Content-Security-Policy",
        "default-src 'self'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; object-src 'none'; frame-ancestors 'none'");
}

static esp_err_t send_page_start(httpd_req_t *request, const char *title)
{
    send_security_headers(request);
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(request, "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'><title>");
    httpd_resp_sendstr_chunk(request, title);
    httpd_resp_sendstr_chunk(request, "</title><style>");
    httpd_resp_sendstr_chunk(request, web_style);
    return httpd_resp_sendstr_chunk(request, "</style><header><h1>CERBERUS SENTINEL</h1></header><main>");
}

static esp_err_t root_get(httpd_req_t *request)
{
    /* Read authentication before sending the first response chunk. ESP-IDF
     * purges request headers once a response begins, including Cookie. */
    web_session_t *session = authenticated_session(request);
    ESP_RETURN_ON_ERROR(send_page_start(request, "Cerberus"), TAG, "page start failed");
    if (!web_status.password_configured) {
        httpd_resp_sendstr_chunk(request,
            "<div class=card><h2>First-run setup</h2><p>Create the Web UI administrator password. There is no default password.</p>"
            "<form method=post action='http://192.168.4.1/setup'><input type=password name=password minlength=12 maxlength=96 required placeholder='12+ character password'>"
            "<input type=password name=confirm minlength=12 maxlength=96 required placeholder='Confirm password'><button>Secure Web UI</button></form></div>");
    } else if (session == NULL) {
        if (strstr(request->uri, "login=failed") != NULL) {
            httpd_resp_sendstr_chunk(request,
                "<div class=card style='border-color:#ff4d4d'><strong style='color:#ff4d4d'>Password incorrect.</strong> Please try again.</div>");
        } else if (strstr(request->uri, "login=locked") != NULL) {
            httpd_resp_sendstr_chunk(request,
                "<div class=card style='border-color:#ffb000'><strong style='color:#ffb000'>Too many attempts.</strong> Wait one minute and try again.</div>");
        }
        httpd_resp_sendstr_chunk(request,
            "<div class=card><h2>Sign in</h2><form method=post action='http://192.168.4.1/login'><input type=password name=password required autocomplete=current-password>"
            "<button>Sign in</button></form><p>Sessions expire after 15 minutes of inactivity.</p></div>");
    } else {
        char csrf[WEB_CSRF_TOKEN_BYTES * 2U + 1U];
        hex_encode(session->csrf, sizeof(session->csrf), csrf);
        httpd_resp_sendstr_chunk(request, "<div class=card><h2>Device dashboard</h2><div id=status>Loading...</div></div>"
            "<div class=card><h2>Radio visibility</h2><button onclick=loadRadio('wifi')>Wi-Fi</button><button onclick=loadRadio('ble')>BLE</button>"
            "<button onclick=loadRadio('154')>802.15.4</button><div id=radio></div></div>"
            "<div class=card><h2>Files and logs</h2><input id=path value='/'><button onclick=files()>List</button><br><input id=filename maxlength=255 placeholder='Entry name'>"
            "<button onclick=fileAction('mkdir')>New folder</button><button onclick=fileAction('touch')>New file</button><button onclick=fileAction('delete')>Delete</button><div id=files></div></div>"
            "<div class=card><h2>Settings</h2><label>Brightness <input id=brightness type=number min=10 max=100></label>"
            "<label>Screen timeout <input id=timeout type=number min=0 max=3600></label><label>Text size <select id=textsize><option value=0>Standard</option><option value=1>Large</option></select></label>"
            "<button onclick=settings()>Save</button></div>"
            "<div class=card><h2>QR generator</h2><input id=qrtext maxlength=180 placeholder='Text or URL'><button onclick=qr()>Generate</button><div id=qr></div></div>"
            "<div class=card><h2>Firmware package storage</h2><input id=firmware type=file accept='.bin,.cerb'><button onclick=upload()>Upload to SD</button><progress id=progress max=100 value=0></progress><div id=uploadStatus></div></div>"
            "<div class=card><h2>Guarded controls</h2><button onclick=control('reboot')>Reboot S3</button><button onclick=control('recovery')>Request recovery</button> <a href=/logout>Sign out</a></div><script>const csrf='");
        httpd_resp_sendstr_chunk(request, csrf);
        httpd_resp_sendstr_chunk(request, "';const el=id=>document.getElementById(id);async function api(u,o={}){o.headers={...(o.headers||{}),'X-CSRF-Token':csrf};let r=await fetch(u,o);if(r.status===401){location.reload();throw Error('session expired')}if(!r.ok)throw Error(await r.text());return r}"
            "async function refreshStatus(){el('status').innerHTML=await(await api('/api/status')).text()}"
            "async function loadRadio(t){el('radio').innerHTML=await(await api('/api/radio?type='+t)).text()}"
            "async function files(){el('files').innerHTML=await(await api('/api/files?path='+encodeURIComponent(el('path').value))).text()}"
            "async function fileAction(a){if(a==='delete'&&!confirm('Delete '+el('filename').value+'?'))return;await api('/api/file',{method:'POST',body:'action='+a+'&path='+encodeURIComponent(el('path').value)+'&name='+encodeURIComponent(el('filename').value)+'&confirm=yes'});files()}"
            "async function settings(){await api('/api/settings',{method:'POST',body:'brightness='+el('brightness').value+'&timeout='+el('timeout').value+'&textsize='+el('textsize').value});refreshStatus()}"
            "async function qr(){el('qr').innerHTML=await(await api('/api/qr?text='+encodeURIComponent(el('qrtext').value))).text()}"
            "function upload(){let f=el('firmware').files[0];if(!f)return;let x=new XMLHttpRequest;x.open('POST','/api/upload?name='+encodeURIComponent(f.name));x.setRequestHeader('X-CSRF-Token',csrf);x.upload.onprogress=e=>el('progress').value=e.loaded/e.total*100;x.onload=()=>el('uploadStatus').textContent=x.responseText;x.send(f)}"
            "async function control(a){if(!confirm('Confirm '+a+'?'))return;await api('/api/'+a,{method:'POST',body:'confirm=yes'})}refreshStatus();</script>");
    }
    httpd_resp_sendstr_chunk(request, "</main>");
    return httpd_resp_sendstr_chunk(request, NULL);
}

static esp_err_t setup_post(httpd_req_t *request)
{
    if (web_status.password_configured) {
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "setup already complete");
    }
    char body[WEB_BODY_MAX];
    char password[100];
    char confirm[100];
    if (read_body(request, body, sizeof(body)) != ESP_OK ||
        !form_value(body, "password", password, sizeof(password)) ||
        !form_value(body, "confirm", confirm, sizeof(confirm)) || strcmp(password, confirm) != 0 ||
        save_password(password) != ESP_OK) {
        memset(password, 0, sizeof(password));
        memset(confirm, 0, sizeof(confirm));
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "passwords must match and contain 12-96 characters");
    }
    memset(password, 0, sizeof(password));
    memset(confirm, 0, sizeof(confirm));
    httpd_resp_set_status(request, "303 See Other");
    httpd_resp_set_hdr(request, "Location", "http://192.168.4.1/");
    return httpd_resp_send(request, NULL, 0);
}

static esp_err_t login_post(httpd_req_t *request)
{
    int64_t now = esp_timer_get_time();
    if (now < login_locked_until_us) {
        httpd_resp_set_status(request, "303 See Other");
        httpd_resp_set_hdr(request, "Location", "http://192.168.4.1/?login=locked");
        return httpd_resp_send(request, NULL, 0);
    }
    char body[WEB_BODY_MAX];
    char password[100];
    if (read_body(request, body, sizeof(body)) != ESP_OK || !form_value(body, "password", password, sizeof(password)) ||
        !password_matches(password)) {
        memset(password, 0, sizeof(password));
        if (++login_failures >= WEB_LOGIN_FAILURE_LIMIT) {
            login_failures = 0U;
            login_locked_until_us = now + WEB_LOGIN_LOCKOUT_US;
        }
        ESP_LOGW(TAG, "administrator login rejected");
        httpd_resp_set_status(request, "303 See Other");
        httpd_resp_set_hdr(request, "Location", "http://192.168.4.1/?login=failed");
        return httpd_resp_send(request, NULL, 0);
    }
    memset(password, 0, sizeof(password));
    login_failures = 0U;
    web_session_t *session = create_session();
    char token[WEB_SESSION_TOKEN_BYTES * 2U + 1U];
    char cookie[128];
    hex_encode(session->token, sizeof(session->token), token);
    snprintf(cookie, sizeof(cookie), "CERBERUS_SESSION=%s; Path=/; HttpOnly", token);
    ESP_LOGI(TAG, "administrator login accepted");
    httpd_resp_set_hdr(request, "Set-Cookie", cookie);
    httpd_resp_set_status(request, "303 See Other");
    httpd_resp_set_hdr(request, "Location", "http://192.168.4.1/");
    return httpd_resp_send(request, NULL, 0);
}

static esp_err_t logout_get(httpd_req_t *request)
{
    web_session_t *session = authenticated_session(request);
    if (session != NULL) session->active = false;
    httpd_resp_set_hdr(request, "Set-Cookie", "CERBERUS_SESSION=; Path=/; HttpOnly; Max-Age=0");
    httpd_resp_set_status(request, "303 See Other");
    httpd_resp_set_hdr(request, "Location", "http://192.168.4.1/");
    return httpd_resp_send(request, NULL, 0);
}

static web_session_t *require_auth(httpd_req_t *request, bool mutation)
{
    web_session_t *session = authenticated_session(request);
    if (session == NULL) {
        httpd_resp_send_err(request, HTTPD_401_UNAUTHORIZED, "authentication required");
        return NULL;
    }
    if (mutation && !csrf_valid(request, session)) {
        httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "invalid CSRF token");
        return NULL;
    }
    send_security_headers(request);
    return session;
}

static esp_err_t status_get(httpd_req_t *request)
{
    if (require_auth(request, false) == NULL) return ESP_OK;
    cerberus_system_health_t health;
    cerberus_system_health_snapshot(&health);
    cerberus_c5_status_t c5 = {0};
    esp_err_t c5_err = c5_spi_link_get_status(&c5);
    cerberus_storage_status_t sd = {0};
    esp_err_t sd_err = cerberus_storage_get_status(&sd);
    const cerberus_settings_t *settings = cerberus_settings_get();
    char output[1400];
    snprintf(output, sizeof(output),
        "<table><tr><th>S3 uptime</th><td>%llu s</td></tr><tr><th>S3 free/min heap</th><td>%lu / %lu bytes</td></tr>"
        "<tr><th>Reset</th><td>%s</td></tr><tr><th>C5</th><td>%s, firmware %lu, protocol %u, uptime %lu s</td></tr>"
        "<tr><th>microSD</th><td>%s, %llu MB free</td></tr><tr><th>Web clients</th><td>%u</td></tr>"
        "<tr><th>Brightness</th><td>%u%%</td></tr></table>",
        (unsigned long long)(health.uptime_ms / 1000ULL), (unsigned long)health.free_internal_bytes,
        (unsigned long)health.minimum_internal_bytes, cerberus_reset_reason_label(health.reset_reason),
        c5_err == ESP_OK ? "online" : "offline", (unsigned long)c5.firmware_version, c5.protocol_version,
        (unsigned long)(c5.uptime_ms / 1000U), sd_err == ESP_OK ? "mounted" : "unavailable",
        (unsigned long long)(sd.free_bytes / (1024ULL * 1024ULL)), web_status.connected_clients,
        settings->brightness_percent);
    return httpd_resp_sendstr(request, output);
}

static esp_err_t query_value(httpd_req_t *request, const char *key, char *output, size_t capacity)
{
    size_t length = httpd_req_get_url_query_len(request) + 1U;
    if (length <= 1U || length > 512U) return ESP_ERR_NOT_FOUND;
    char query[512];
    ESP_RETURN_ON_ERROR(httpd_req_get_url_query_str(request, query, length), TAG, "query read failed");
    ESP_RETURN_ON_ERROR(httpd_query_key_value(query, key, output, capacity), TAG, "query key failed");
    url_decode(output);
    return ESP_OK;
}

static esp_err_t radio_get(httpd_req_t *request)
{
    if (require_auth(request, false) == NULL) return ESP_OK;
    char type[12];
    if (query_value(request, "type", type, sizeof(type)) != ESP_OK) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "missing type");
    httpd_resp_set_type(request, "text/html");
    httpd_resp_sendstr_chunk(request, "<table><tr><th>Name/address</th><th>Channel</th><th>Signal</th></tr>");
    char row[320];
    if (strcmp(type, "wifi") == 0) {
        cerberus_wifi_scan_status_payload_t status;
        if (c5_spi_link_get_wifi_scan_status(&status) == ESP_OK) {
            cerberus_wifi_radio_stats_payload_t stats;
            if (status.state == CERBERUS_WIFI_SCAN_COMPLETE &&
                c5_spi_link_get_wifi_radio_stats(&stats) == ESP_OK &&
                stats.generation == status.generation) {
                snprintf(row, sizeof(row),
                    "<tr><td colspan='3'>Scan %u: %u heard, %u analyzed%s, %u 2.4 GHz, %u 5 GHz, %u open, %u secured, %u hidden, %lu ms</td></tr>",
                    stats.generation, stats.total_count, stats.analyzed_count,
                    stats.truncated ? " (truncated)" : "", stats.count_24_ghz,
                    stats.count_5_ghz, stats.open_count, stats.secured_count,
                    stats.hidden_count, (unsigned long)stats.scan_duration_ms);
                httpd_resp_sendstr_chunk(request, row);
            }
            cerberus_wifi_defense_status_payload_t defense;
            if (c5_spi_link_get_wifi_defense_status(&defense) == ESP_OK &&
                (defense.state == CERBERUS_WIFI_DEFENSE_RUNNING ||
                 defense.state == CERBERUS_WIFI_DEFENSE_COMPLETE)) {
                snprintf(row, sizeof(row),
                    "<tr><td colspan='3'>Defensive survey: %u/%u channels, %u flagged, %lu ms. Heuristic indicators only; no payloads or addresses retained.</td></tr>",
                    defense.completed_count, defense.result_count, defense.flagged_count,
                    (unsigned long)defense.elapsed_ms);
                httpd_resp_sendstr_chunk(request, row);
                for (uint8_t index = 0U; index < defense.completed_count; ++index) {
                    cerberus_wifi_defense_result_payload_t item;
                    if (c5_spi_link_get_wifi_defense_result(
                            defense.generation, index, &item) == ESP_OK) {
                        snprintf(row, sizeof(row),
                            "<tr><td>Aggregate frames M/C/D %u/%u/%u; beacon %u/s; deauth/disassoc %u/%u; flags 0x%02X</td><td>%u</td><td>noise %d dBm, peak %d dBm</td></tr>",
                            item.management_frames, item.control_frames, item.data_frames,
                            item.beacon_rate_per_sec, item.deauth_frames, item.disassoc_frames,
                            item.assessment_flags, item.channel, item.average_noise_floor,
                            item.strongest_rssi);
                        httpd_resp_sendstr_chunk(request, row);
                    }
                }
            }
            for (uint8_t index = 0U; index < status.stored_count && index < 32U; ++index) {
                cerberus_c5_wifi_result_t item;
                if (c5_spi_link_get_wifi_scan_result(status.generation, index, &item) == ESP_OK) {
                    char escaped[160];
                    html_escape(item.ssid[0] ? item.ssid : "[hidden]", escaped, sizeof(escaped));
                    unsigned strength = item.rssi <= -100 ? 0U : (item.rssi >= -30 ? 100U : (unsigned)((item.rssi + 100) * 100 / 70));
                    snprintf(row, sizeof(row), "<tr><td>%s</td><td>%u</td><td>%d dBm<div style='background:#20ff75;height:6px;width:%u%%'></div></td></tr>", escaped, item.channel, item.rssi, strength);
                    httpd_resp_sendstr_chunk(request, row);
                }
            }
        }
    } else if (strcmp(type, "ble") == 0) {
        cerberus_ble_scan_status_payload_t status;
        if (cerberus_s3_ble_scanner_get_status(&status) == ESP_OK &&
            status.state == CERBERUS_BLE_SCAN_COMPLETE) {
            for (uint8_t index = 0U; index < status.stored_count && index < 32U; ++index) {
                cerberus_c5_ble_result_t item;
                if (cerberus_s3_ble_scanner_get_result(status.generation, index, &item) == ESP_OK) {
                    char escaped[160];
                    char identifier[24];
                    html_escape(item.name[0] ? item.name : "[unnamed]", escaped, sizeof(escaped));
                    cerberus_privacy_format_ble_identifier(
                        item.address,
                        cerberus_settings_get()->privacy_mask_addresses,
                        identifier,
                        sizeof(identifier));
                    unsigned strength = item.rssi <= -100 ? 0U : (item.rssi >= -30 ? 100U : (unsigned)((item.rssi + 100) * 100 / 70));
                    snprintf(row, sizeof(row), "<tr><td>%s<br>%s</td><td>BLE</td><td>%d dBm<div style='background:#20ff75;height:6px;width:%u%%'></div></td></tr>",
                        escaped, identifier, item.rssi, strength);
                    httpd_resp_sendstr_chunk(request, row);
                }
            }
        }
    } else {
        cerberus_ieee802154_energy_scan_status_payload_t status;
        cerberus_ieee802154_energy_scan_result_payload_t result;
        if (c5_spi_link_get_ieee802154_energy_scan_status(&status) == ESP_OK &&
            c5_spi_link_get_ieee802154_energy_scan_result(status.generation, &result) == ESP_OK) {
            for (uint8_t index = 0U; index < result.channel_count; ++index) {
                unsigned strength = result.power_dbm[index] <= -100 ? 0U : (result.power_dbm[index] >= -30 ? 100U : (unsigned)((result.power_dbm[index] + 100) * 100 / 70));
                snprintf(row, sizeof(row), "<tr><td>802.15.4 energy</td><td>%u</td><td>%d dBm<div style='background:#20ff75;height:6px;width:%u%%'></div></td></tr>", result.first_channel + index, result.power_dbm[index], strength);
                httpd_resp_sendstr_chunk(request, row);
            }
        }
    }
    httpd_resp_sendstr_chunk(request, "</table>");
    return httpd_resp_sendstr_chunk(request, NULL);
}

static esp_err_t files_get(httpd_req_t *request)
{
    if (require_auth(request, false) == NULL) return ESP_OK;
    char path[CERBERUS_STORAGE_PATH_MAX];
    if (query_value(request, "path", path, sizeof(path)) != ESP_OK) snprintf(path, sizeof(path), "/");
    cerberus_storage_entry_t entries[32];
    size_t count = 0U;
    esp_err_t err = cerberus_storage_list_directory(path, entries, 32U, &count);
    if (err != ESP_OK) return httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "directory unavailable");
    httpd_resp_set_type(request, "text/html");
    httpd_resp_sendstr_chunk(request, "<table><tr><th>Name</th><th>Size</th></tr>");
    char row[800];
    for (size_t index = 0U; index < count; ++index) {
        char escaped[320];
        char encoded_name[CERBERUS_STORAGE_NAME_MAX * 3U];
        char encoded_path[CERBERUS_STORAGE_PATH_MAX * 3U];
        html_escape(entries[index].name, escaped, sizeof(escaped));
        url_encode(entries[index].name, encoded_name, sizeof(encoded_name));
        url_encode(path, encoded_path, sizeof(encoded_path));
        if (entries[index].is_directory) {
            snprintf(row, sizeof(row), "<tr><td>[DIR] %.300s</td><td></td></tr>", escaped);
        } else {
            snprintf(row, sizeof(row), "<tr><td><a href='/api/download?path=%.190s&name=%.190s'>%.180s</a></td><td>%llu</td></tr>",
                encoded_path, encoded_name, escaped, (unsigned long long)entries[index].size_bytes);
        }
        httpd_resp_sendstr_chunk(request, row);
    }
    httpd_resp_sendstr_chunk(request, "</table>");
    return httpd_resp_sendstr_chunk(request, NULL);
}

static esp_err_t settings_post(httpd_req_t *request)
{
    if (require_auth(request, true) == NULL) return ESP_OK;
    char body[WEB_BODY_MAX], brightness[8], timeout[8], text_size[8];
    if (read_body(request, body, sizeof(body)) != ESP_OK || !form_value(body, "brightness", brightness, sizeof(brightness)) ||
        !form_value(body, "timeout", timeout, sizeof(timeout)) ||
        !form_value(body, "textsize", text_size, sizeof(text_size))) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "invalid settings");
    unsigned long brightness_value = strtoul(brightness, NULL, 10);
    unsigned long timeout_value = strtoul(timeout, NULL, 10);
    unsigned long text_size_value = strtoul(text_size, NULL, 10);
    if (brightness_value < CERBERUS_SETTINGS_MIN_BRIGHTNESS || brightness_value > CERBERUS_SETTINGS_MAX_BRIGHTNESS ||
        timeout_value > 3600U || text_size_value > 1U ||
        cerberus_settings_set_brightness((uint8_t)brightness_value) != ESP_OK ||
        cerberus_settings_set_screen_timeout((uint16_t)timeout_value) != ESP_OK ||
        cerberus_settings_set_accessibility_size((uint8_t)text_size_value) != ESP_OK)
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "settings rejected");
    return httpd_resp_sendstr(request, "saved");
}

static esp_err_t file_post(httpd_req_t *request)
{
    if (require_auth(request, true) == NULL) return ESP_OK;
    char body[WEB_BODY_MAX], action[16], path[CERBERUS_STORAGE_PATH_MAX], name[CERBERUS_STORAGE_NAME_MAX], confirm[8];
    if (read_body(request, body, sizeof(body)) != ESP_OK || !form_value(body, "action", action, sizeof(action)) ||
        !form_value(body, "path", path, sizeof(path)) || !form_value(body, "name", name, sizeof(name)))
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "invalid file action");
    esp_err_t err;
    if (strcmp(action, "mkdir") == 0) err = cerberus_storage_create_directory(path, name);
    else if (strcmp(action, "touch") == 0) err = cerberus_storage_create_empty_file(path, name);
    else if (strcmp(action, "delete") == 0 && form_value(body, "confirm", confirm, sizeof(confirm)) && strcmp(confirm, "yes") == 0)
        err = cerberus_storage_delete_entry(path, name);
    else err = ESP_ERR_INVALID_ARG;
    if (err != ESP_OK) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, esp_err_to_name(err));
    return httpd_resp_sendstr(request, "complete");
}

static esp_err_t download_get(httpd_req_t *request)
{
    if (require_auth(request, false) == NULL) return ESP_OK;
    char path[CERBERUS_STORAGE_PATH_MAX];
    char name[CERBERUS_STORAGE_NAME_MAX];
    if (query_value(request, "path", path, sizeof(path)) != ESP_OK ||
        query_value(request, "name", name, sizeof(name)) != ESP_OK) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "path and name required");
    }
    httpd_resp_set_type(request, "application/octet-stream");
    httpd_resp_set_hdr(request, "Content-Disposition", "attachment");
    uint8_t *buffer = heap_caps_malloc(4096U, MALLOC_CAP_8BIT);
    if (buffer == NULL) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "memory unavailable");
    uint64_t offset = 0U, file_size = 0U;
    esp_err_t err = ESP_OK;
    do {
        size_t count = 0U;
        err = cerberus_storage_read_file_chunk(path, name, offset, buffer, 4096U, &count, &file_size);
        if (err != ESP_OK) break;
        if (count > 0U && httpd_resp_send_chunk(request, (const char *)buffer, count) != ESP_OK) {
            err = ESP_FAIL;
            break;
        }
        offset += count;
        if (count == 0U) break;
    } while (offset < file_size);
    free(buffer);
    if (err != ESP_OK) return err;
    return httpd_resp_send_chunk(request, NULL, 0U);
}

static esp_err_t qr_get(httpd_req_t *request)
{
    if (require_auth(request, false) == NULL) return ESP_OK;
    char text[181];
    if (query_value(request, "text", text, sizeof(text)) != ESP_OK || text[0] == '\0')
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "1-180 characters required");
    uint16_t modules = 0U;
    esp_err_t err = cerberus_qr_encode_text(text, web_qr_modules, sizeof(web_qr_modules), &modules);
    memset(text, 0, sizeof(text));
    if (err != ESP_OK) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "QR payload too large");
    httpd_resp_set_type(request, "image/svg+xml");
    char line[224];
    snprintf(line, sizeof(line), "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 %u %u' width='280' height='280'><rect width='100%%' height='100%%' fill='white'/><g fill='black' transform='translate(4 4)'>", modules + 8U, modules + 8U);
    httpd_resp_sendstr_chunk(request, line);
    for (uint16_t y = 0U; y < modules; ++y) {
        for (uint16_t x = 0U; x < modules;) {
            if (web_qr_modules[(size_t)y * modules + x] == 0U) { ++x; continue; }
            uint16_t start = x;
            while (x < modules && web_qr_modules[(size_t)y * modules + x] != 0U) ++x;
            snprintf(line, sizeof(line), "<rect x='%u' y='%u' width='%u' height='1'/>", start, y, x - start);
            httpd_resp_sendstr_chunk(request, line);
        }
    }
    memset(web_qr_modules, 0, sizeof(web_qr_modules));
    httpd_resp_sendstr_chunk(request, "</g></svg>");
    return httpd_resp_sendstr_chunk(request, NULL);
}

static esp_err_t upload_post(httpd_req_t *request)
{
    if (require_auth(request, true) == NULL) return ESP_OK;
    char name[CERBERUS_STORAGE_NAME_MAX];
    if (query_value(request, "name", name, sizeof(name)) != ESP_OK || request->content_len <= 0 ||
        (size_t)request->content_len > CERBERUS_STORAGE_FIRMWARE_MAX_BYTES) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "valid .bin/.cerb file up to 4 MiB required");
    }
    uint8_t *data = heap_caps_malloc((size_t)request->content_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (data == NULL) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "upload memory unavailable");
    size_t received = 0U;
    while (received < (size_t)request->content_len) {
        int count = httpd_req_recv(request, (char *)data + received, request->content_len - received);
        if (count == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (count <= 0) {
            free(data);
            return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "upload interrupted");
        }
        received += (size_t)count;
    }
    cerberus_firmware_package_info_t info;
    esp_err_t err = cerberus_storage_write_firmware_package(name, data, received, &info);
    memset(data, 0, received);
    free(data);
    if (err != ESP_OK) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "package rejected or already exists");
    char response[384];
    snprintf(response, sizeof(response), "stored /CERBERUS/FIRMWARE/%s (%llu bytes, %u segments)", name,
        (unsigned long long)info.size_bytes, info.segment_count);
    return httpd_resp_sendstr(request, response);
}

static esp_err_t control_post(httpd_req_t *request, bool recovery)
{
    if (require_auth(request, true) == NULL) return ESP_OK;
    char body[WEB_BODY_MAX], confirm[8];
    if (read_body(request, body, sizeof(body)) != ESP_OK || !form_value(body, "confirm", confirm, sizeof(confirm)) || strcmp(confirm, "yes") != 0)
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "explicit confirmation required");
    if (recovery && !cerberus_recovery_download_mode_available()) return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "recovery unavailable");
    (void)cerberus_storage_log_event(CERBERUS_LOG_WARNING, "web", recovery ? "recovery" : "reboot", "authenticated request confirmed");
    httpd_resp_sendstr(request, recovery ? "entering recovery" : "rebooting");
    vTaskDelay(pdMS_TO_TICKS(250));
    if (recovery) (void)cerberus_recovery_enter_download_mode();
    esp_restart();
}

static esp_err_t reboot_post(httpd_req_t *request) { return control_post(request, false); }
static esp_err_t recovery_post(httpd_req_t *request) { return control_post(request, true); }

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base;
    if (id == WIFI_EVENT_AP_STACONNECTED) web_status.connected_clients++;
    else if (id == WIFI_EVENT_AP_STADISCONNECTED && web_status.connected_clients > 0U) web_status.connected_clients--;
}

static esp_err_t register_uri(const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *))
{
    httpd_uri_t config = {.uri = uri, .method = method, .handler = handler, .user_ctx = NULL};
    return httpd_register_uri_handler(web_server, &config);
}

esp_err_t cerberus_web_ui_stop(void)
{
    if (web_server != NULL) {
        esp_err_t err = httpd_stop(web_server);
        if (err != ESP_OK) return err;
        web_server = NULL;
    }
    web_status.running = false;
    if (web_wifi_started) {
        esp_err_t err = esp_wifi_stop();
        if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) return err;
        web_wifi_started = false;
    }
    if (web_handler_registered) {
        esp_err_t err = esp_event_handler_instance_unregister(
            WIFI_EVENT, ESP_EVENT_ANY_ID, web_wifi_handler);
        if (err != ESP_OK) return err;
        web_handler_registered = false;
    }
    if (web_wifi_initialized) {
        esp_err_t err = esp_wifi_deinit();
        if (err != ESP_OK) return err;
        web_wifi_initialized = false;
    }
    if (web_netif != NULL) {
        esp_netif_destroy_default_wifi(web_netif);
        web_netif = NULL;
    }
    if(web_wifi_claimed){
        esp_err_t err=cerberus_s3_wifi_release(CERBERUS_S3_WIFI_WEB);
        if(err!=ESP_OK)return err;
        web_wifi_claimed=false;
    }
    memset(web_sessions, 0, sizeof(web_sessions));
    memset(password_salt, 0, sizeof(password_salt));
    memset(password_hash, 0, sizeof(password_hash));
    memset(&web_status, 0, sizeof(web_status));
    return ESP_OK;
}

esp_err_t cerberus_web_ui_start(void)
{
    if (web_status.running) return ESP_OK;
    /* Clear resources from any earlier partial startup before retrying. */
    ESP_RETURN_ON_ERROR(cerberus_web_ui_stop(), TAG, "web cleanup failed");
    esp_err_t err = ESP_OK;
#define WEB_START_TRY(call) do { err = (call); if (err != ESP_OK) goto failed; } while (0)
    WEB_START_TRY(cerberus_s3_wifi_claim(CERBERUS_S3_WIFI_WEB));
    web_wifi_claimed=true;
    /* Reserve Wi-Fi before using hardware entropy or initializing its driver.
     * No S3 ADC service runs here; release SAR before initializing Wi-Fi. */
    bootloader_random_enable();
    err = load_or_create_configuration();
    bootloader_random_disable();
    if (err != ESP_OK) goto failed;
    WEB_START_TRY(cerberus_s3_wifi_prepare(CERBERUS_S3_WIFI_WEB));
    web_netif = esp_netif_create_default_wifi_ap();
    if (web_netif == NULL) { err = ESP_ERR_NO_MEM; goto failed; }
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    WEB_START_TRY(esp_wifi_init(&init));
    web_wifi_initialized = true;
    /* Do not persist an active AP mode across boots. */
    WEB_START_TRY(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    uint8_t mac[6];
    WEB_START_TRY(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));
    snprintf(web_status.ssid, sizeof(web_status.ssid), "Cerberus-%02X%02X%02X", mac[3], mac[4], mac[5]);
    snprintf(web_status.address, sizeof(web_status.address), "http://192.168.4.1/");
    wifi_config_t config = {0};
    size_t ssid_length = strnlen(web_status.ssid, sizeof(config.ap.ssid));
    size_t password_length = strnlen(web_status.access_point_password, sizeof(config.ap.password));
    memcpy(config.ap.ssid, web_status.ssid, ssid_length);
    memcpy(config.ap.password, web_status.access_point_password, password_length);
    config.ap.ssid_len = ssid_length;
    config.ap.channel = 1U;
    config.ap.max_connection = 4U;
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    config.ap.pmf_cfg.required = true;
    WEB_START_TRY(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL, &web_wifi_handler));
    web_handler_registered = true;
    WEB_START_TRY(esp_wifi_set_mode(WIFI_MODE_AP));
    WEB_START_TRY(esp_wifi_set_config(WIFI_IF_AP, &config));
    WEB_START_TRY(esp_wifi_start());
    web_wifi_started = true;
    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.stack_size = 16384U;
    server_config.max_uri_handlers = 16U;
    server_config.lru_purge_enable = true;
    WEB_START_TRY(httpd_start(&web_server, &server_config));
    WEB_START_TRY(register_uri("/", HTTP_GET, root_get));
    WEB_START_TRY(register_uri("/setup", HTTP_POST, setup_post));
    WEB_START_TRY(register_uri("/login", HTTP_POST, login_post));
    WEB_START_TRY(register_uri("/logout", HTTP_GET, logout_get));
    WEB_START_TRY(register_uri("/api/status", HTTP_GET, status_get));
    WEB_START_TRY(register_uri("/api/radio", HTTP_GET, radio_get));
    WEB_START_TRY(register_uri("/api/files", HTTP_GET, files_get));
    WEB_START_TRY(register_uri("/api/download", HTTP_GET, download_get));
    WEB_START_TRY(register_uri("/api/qr", HTTP_GET, qr_get));
    WEB_START_TRY(register_uri("/api/upload", HTTP_POST, upload_post));
    WEB_START_TRY(register_uri("/api/settings", HTTP_POST, settings_post));
    WEB_START_TRY(register_uri("/api/file", HTTP_POST, file_post));
    WEB_START_TRY(register_uri("/api/reboot", HTTP_POST, reboot_post));
    WEB_START_TRY(register_uri("/api/recovery", HTTP_POST, recovery_post));
    web_status.running = true;
    ESP_LOGI(TAG, "Sentinel local dashboard ready at %s SSID=%s", web_status.address, web_status.ssid);
#undef WEB_START_TRY
    return ESP_OK;
failed:
    ESP_LOGW(TAG, "local dashboard start failed: %s", esp_err_to_name(err));
    (void)cerberus_web_ui_stop();
    return err;
}

void cerberus_web_ui_get_status(cerberus_web_ui_status_t *status)
{
    if (status != NULL) {
        *status = web_status;
        status->radio_active = web_wifi_started;
    }
}

esp_err_t cerberus_web_ui_factory_reset(void)
{
    ESP_RETURN_ON_ERROR(cerberus_web_ui_stop(), TAG, "stop local dashboard before reset");
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(WEB_NAMESPACE, NVS_READWRITE, &handle), TAG, "web NVS open failed");
    esp_err_t err = nvs_erase_all(handle);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) {
        memset(password_salt, 0, sizeof(password_salt));
        memset(password_hash, 0, sizeof(password_hash));
        memset(web_sessions, 0, sizeof(web_sessions));
        web_status.password_configured = false;
    }
    return err;
}
