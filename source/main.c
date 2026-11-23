
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <curl/curl.h>
#include "altcha.h"

#define API_BASE       "https://api.fluxer.app/v1"
#define SOC_ALIGN      0x1000
#define SOC_BUFFERSIZE 0x100000
#define HTTP_BODY_MAX  16384
#define TOKEN_MAX      2048
#define INPUT_MAX      384

#define COL_BG       C2D_Color32(29, 31, 36, 255)
#define COL_PANEL    C2D_Color32(39, 42, 49, 255)
#define COL_PANEL2   C2D_Color32(49, 53, 62, 255)
#define COL_ACCENT   C2D_Color32(115, 83, 220, 255)
#define COL_TEXT     C2D_Color32(245, 246, 248, 255)
#define COL_MUTED    C2D_Color32(173, 178, 190, 255)
#define COL_GREEN    C2D_Color32(88, 190, 133, 255)
#define COL_RED      C2D_Color32(225, 104, 104, 255)

static u32 *soc_buffer = NULL;
static int network_ready = 0;
static int sslc_ready = 0;

static char session_token[TOKEN_MAX];
static char account_name[96] = "Not signed in";
static char status_text[256] =
    "Connect to Wi-Fi, then sign in.";

static char mfa_ticket[256];

static int app_state = 0;
/*
    0 = Login screen
    1 = MFA screen
    2 = Signed in
    3 = Error screen
*/

static C2D_Font ui_font;
static C2D_TextBuf text_buffer;

/* ---------------------------------------------------------
   GENERAL HELPERS
   --------------------------------------------------------- */

static void set_error(char *detail, size_t size,
                      const char *stage, Result rc)
{
    if (detail && size > 0) {
        snprintf(detail, size, "%s: 0x%08lX",
                 stage, (unsigned long)rc);
    }
}

static int json_string(const char *json, const char *key,
                       char *out, size_t out_size)
{
    char needle[80];
    const char *p;
    size_t n = 0;

    if (!json || !key || !out || out_size < 2)
        return 0;

    snprintf(needle, sizeof(needle), "\"%s\"", key);

    p = strstr(json, needle);

    if (!p)
        return 0;

    p = strchr(p + strlen(needle), ':');

    if (!p)
        return 0;

    p++;

    while (*p == ' ' || *p == '\t' ||
           *p == '\r' || *p == '\n')
        p++;

    if (*p != '"')
        return 0;

    p++;

    while (*p && *p != '"' && n + 1 < out_size) {
        if (*p == '\\' && p[1]) {
            p++;

            if (*p == 'n')
                out[n++] = '\n';
            else if (*p == 'r')
                out[n++] = '\r';
            else if (*p == 't')
                out[n++] = '\t';
            else
                out[n++] = *p;

            p++;
        } else {
            out[n++] = *p++;
        }
    }

    out[n] = '\0';

    return (*p == '"');
}

static int json_escape(const char *input,
                       char *output, size_t output_size)
{
    size_t n = 0;

    if (!input || !output || output_size < 3)
        return 0;

    while (*input) {
        unsigned char c = (unsigned char)*input++;

        if (c == '"' || c == '\\') {
            if (n + 2 >= output_size)
                return 0;

            output[n++] = '\\';
            output[n++] = (char)c;
        }
        else if (c < 32) {
            if (n + 1 >= output_size)
                return 0;

            output[n++] = ' ';
        }
        else {
            if (n + 1 >= output_size)
                return 0;

            output[n++] = (char)c;
        }
    }

    output[n] = '\0';
    return 1;
}

/* ---------------------------------------------------------
   NETWORK - libcurl + mbedTLS
   --------------------------------------------------------- */

typedef struct {
    char *data;
    size_t capacity;
    size_t used;
} ResponseBuffer;

static size_t fluxer_curl_write_callback(
    char *ptr, size_t size, size_t nmemb, void *userdata)
{
    ResponseBuffer *buffer = (ResponseBuffer *)userdata;
    size_t bytes;

    if (!buffer || !buffer->data || size == 0 ||
        nmemb > ((size_t)-1) / size)
        return 0;

    bytes = size * nmemb;

    if (bytes > buffer->capacity - buffer->used - 1)
        return 0;

    memcpy(buffer->data + buffer->used, ptr, bytes);
    buffer->used += bytes;
    buffer->data[buffer->used] = '\0';

    return bytes;
}

static int append_curl_header(
    struct curl_slist **headers, const char *value)
{
    struct curl_slist *next;

    next = curl_slist_append(*headers, value);
    if (!next)
        return 0;

    *headers = next;
    return 1;
}

static Result init_network(void)
{
    Result rc;

    soc_buffer = (u32 *)memalign(SOC_ALIGN, SOC_BUFFERSIZE);

    if (!soc_buffer)
        return -1;

    rc = socInit(soc_buffer, SOC_BUFFERSIZE);

    if (R_FAILED(rc)) {
        free(soc_buffer);
        soc_buffer = NULL;
        return rc;
    }

    /*
       mbedTLS in devkitPro's 3DS port obtains secure random bytes
       through the 3DS SSL service. Start it before any TLS request.
    */
    rc = sslcInit(0);
    if (R_FAILED(rc)) {
        socExit();
        free(soc_buffer);
        soc_buffer = NULL;
        return rc;
    }
    sslc_ready = 1;

    /* libcurl uses libctru sockets and mbedTLS on the 3DS. */
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        socExit();
        free(soc_buffer);
        soc_buffer = NULL;
        return -1;
    }

    network_ready = 1;
    return 0;
}

static void cleanup_network(void)
{
    if (network_ready) {
        curl_global_cleanup();
        network_ready = 0;
    }

    if (sslc_ready) {
        sslcExit();
        sslc_ready = 0;
    }

    if (soc_buffer) {
        socExit();
        free(soc_buffer);
        soc_buffer = NULL;
    }
}

static Result api_request(
    HTTPC_RequestMethod method,
    const char *path,
    const char *body,
    const char *auth_token,
    const char *captcha_token,
    char *response,
    size_t response_size,
    u32 *http_status,
    char *detail,
    size_t detail_size)
{
    CURL *easy = NULL;
    CURLcode cc;
    struct curl_slist *headers = NULL;
    char url[512];
    char auth_header[TOKEN_MAX + 16];
    char captcha_header[4200];
    ResponseBuffer output;
    char curl_error[CURL_ERROR_SIZE];
    long status = 0;
    long os_error = 0;
    long verify_result = 0;
    Result result = -1;

    if (detail && detail_size)
        detail[0] = '\0';

    if (!network_ready || !path || !response ||
        response_size < 2) {
        set_error(detail, detail_size, "Invalid request", -1);
        return -1;
    }

    snprintf(url, sizeof(url), "%s%s", API_BASE, path);
    response[0] = '\0';

    if (http_status)
        *http_status = 0;

    output.data = response;
    output.capacity = response_size;
    output.used = 0;
    curl_error[0] = '\0';

    easy = curl_easy_init();
    if (!easy) {
        if (detail && detail_size)
            snprintf(detail, detail_size, "Could not create HTTPS client");
        goto done;
    }

    if (!append_curl_header(&headers, "Accept: application/json")) {
        if (detail && detail_size)
            snprintf(detail, detail_size, "Could not create HTTP headers");
        goto done;
    }

    if (auth_token && auth_token[0]) {
        snprintf(auth_header, sizeof(auth_header),
                 "Authorization: Bearer %s", auth_token);
        if (!append_curl_header(&headers, auth_header)) {
            if (detail && detail_size)
                snprintf(detail, detail_size, "Could not create authorization header");
            goto done;
        }
    }

    if (captcha_token && captcha_token[0]) {
        snprintf(captcha_header, sizeof(captcha_header),
                 "X-Captcha-Token: %s", captcha_token);
        if (!append_curl_header(&headers, captcha_header)) {
            if (detail && detail_size)
                snprintf(detail, detail_size, "Could not create CAPTCHA header");
            goto done;
        }
    }

    if (body) {
        if (!append_curl_header(&headers,
                                "Content-Type: application/json")) {
            if (detail && detail_size)
                snprintf(detail, detail_size, "Could not create content-type header");
            goto done;
        }
    }

#define CURL_SETOPT(option, value) do { \
        cc = curl_easy_setopt(easy, option, value); \
        if (cc != CURLE_OK) { \
            if (detail && detail_size) \
                snprintf(detail, detail_size, "%s: %s", \
                         #option, curl_easy_strerror(cc)); \
            goto done; \
        } \
    } while (0)

    CURL_SETOPT(CURLOPT_URL, url);
    CURL_SETOPT(CURLOPT_HTTPHEADER, headers);
    CURL_SETOPT(CURLOPT_WRITEFUNCTION, fluxer_curl_write_callback);
    CURL_SETOPT(CURLOPT_WRITEDATA, &output);
    CURL_SETOPT(CURLOPT_USERAGENT, "Fluxer3DS/0.4");
    CURL_SETOPT(CURLOPT_ERRORBUFFER, curl_error);
    CURL_SETOPT(CURLOPT_CAINFO, "sdmc:/config/ssl/cacert.pem");
    CURL_SETOPT(CURLOPT_NOSIGNAL, 1L);
    CURL_SETOPT(CURLOPT_CONNECTTIMEOUT, 25L);
    CURL_SETOPT(CURLOPT_TIMEOUT, 75L);

    /* Require modern TLS and keep certificate/hostname checks enabled. */
    CURL_SETOPT(CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
    CURL_SETOPT(CURLOPT_SSL_VERIFYPEER, 1L);
    CURL_SETOPT(CURLOPT_SSL_VERIFYHOST, 2L);

    if (method == HTTPC_METHOD_POST) {
        CURL_SETOPT(CURLOPT_POST, 1L);
        if (body) {
            CURL_SETOPT(CURLOPT_POSTFIELDS, body);
            CURL_SETOPT(CURLOPT_POSTFIELDSIZE, (long)strlen(body));
        }
    } else {
        CURL_SETOPT(CURLOPT_HTTPGET, 1L);
    }

    cc = curl_easy_perform(easy);
    if (cc != CURLE_OK) {
        curl_easy_getinfo(easy, CURLINFO_OS_ERRNO, &os_error);
        curl_easy_getinfo(easy, CURLINFO_SSL_VERIFYRESULT, &verify_result);
        if (detail && detail_size) {
            if (curl_error[0])
                snprintf(detail, detail_size,
                         "HTTPS: %.72s (curl %d, OS %ld, cert %ld)",
                         curl_error, (int)cc, os_error, verify_result);
            else
                snprintf(detail, detail_size,
                         "HTTPS: %s (curl %d, OS %ld, cert %ld)",
                         curl_easy_strerror(cc), (int)cc,
                         os_error, verify_result);
        }
        goto done;
    }

    cc = curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
    if (cc != CURLE_OK) {
        if (detail && detail_size)
            snprintf(detail, detail_size, "Reading HTTP status: %s",
                     curl_easy_strerror(cc));
        goto done;
    }

    if (http_status)
        *http_status = (u32)status;

    result = 0;

#undef CURL_SETOPT

done:
    if (headers)
        curl_slist_free_all(headers);
    if (easy)
        curl_easy_cleanup(easy);

    return result;
}

/* ---------------------------------------------------------
   NINTENDO 3DS KEYBOARD
   --------------------------------------------------------- */

static int keyboard_input(
    const char *hint,
    char *output,
    size_t output_size,
    int password_mode)
{
    SwkbdState keyboard;

    memset(output, 0, output_size);

    swkbdInit(
        &keyboard,
        SWKBD_TYPE_NORMAL,
        2,
        (int)output_size - 1
    );

    swkbdSetHintText(&keyboard, hint);

    if (password_mode) {
        swkbdSetPasswordMode(
            &keyboard, SWKBD_PASSWORD_HIDE);
    }

    SwkbdButton result = swkbdInputText(
        &keyboard, output, output_size);

    if (result != SWKBD_BUTTON_CONFIRM) {
        memset(output, 0, output_size);
        return 0;
    }

    return output[0] != '\0';
}

/* ---------------------------------------------------------
   ACCOUNT LOGIN
   --------------------------------------------------------- */

static void show_api_error(
    u32 http_status,
    const char *response)
{
    char error_code[96];

    error_code[0] = '\0';

    json_string(
        response, "code",
        error_code, sizeof(error_code));

    if (strcmp(error_code, "INVALID_EMAIL_OR_PASSWORD") == 0 ||
        strcmp(error_code, "INVALID_LOGIN_OR_PASSWORD") == 0) {
        snprintf(status_text, sizeof(status_text),
                 "Incorrect email or password.");
    }
    else if (strcmp(error_code, "CAPTCHA_REQUIRED") == 0 ||
             strcmp(error_code, "INVALID_CAPTCHA") == 0) {
        snprintf(status_text, sizeof(status_text),
                 "Fluxer requires CAPTCHA verification. "
                 "This flow is not implemented yet.");
    }
    else if (strcmp(error_code, "IP_AUTHORIZATION_REQUIRED") == 0) {
        snprintf(status_text, sizeof(status_text),
                 "Check your email to authorize this new IP. "
                 "Authorization polling is not implemented yet.");
    }
    else if (strcmp(error_code, "SSO_REQUIRED") == 0) {
        snprintf(status_text, sizeof(status_text),
                 "This account requires organization sign-in.");
    }
    else {
        snprintf(status_text, sizeof(status_text),
                 "Fluxer rejected login. HTTP %lu. %.65s",
                 (unsigned long)http_status,
                 error_code[0] ? error_code : "Unknown error");
    }

    app_state = 3;
}

static int accept_auth_response(
    const char *response)
{
    char token[TOKEN_MAX];
    char ticket[256];

    token[0] = '\0';
    ticket[0] = '\0';

    /*
       Normal successful login returns a token.
    */
    if (json_string(
            response, "token",
            token, sizeof(token))) {

        snprintf(session_token,
                 sizeof(session_token), "%s", token);

        memset(token, 0, sizeof(token));

        return 1;
    }

    /*
       MFA response returns mfa=true and a ticket.
    */
    if (strstr(response, "\"mfa\":true") ||
        strstr(response, "\"mfa\": true")) {

        if (json_string(
                response, "ticket",
                ticket, sizeof(ticket))) {

            snprintf(mfa_ticket,
                     sizeof(mfa_ticket), "%s", ticket);

            memset(ticket, 0, sizeof(ticket));

            app_state = 1;

            snprintf(status_text, sizeof(status_text),
                     "Enter your authenticator code.");

            return 2;
        }
    }

    return 0;
}

static void verify_session(void)
{
    char response[HTTP_BODY_MAX];
    char name[96];
    char detail[128];

    u32 http_status = 0;

    Result rc = api_request(
        HTTPC_METHOD_GET,
        "/users/@me",
        NULL,
        session_token,
        NULL,
        response,
        sizeof(response),
        &http_status,
        detail,
        sizeof(detail)
    );

    if (R_FAILED(rc)) {
        memset(session_token, 0, sizeof(session_token));

        snprintf(status_text, sizeof(status_text),
                 "Account check failed: %.100s", detail);

        app_state = 3;
        return;
    }

    if (http_status != 200) {
        memset(session_token, 0, sizeof(session_token));

        snprintf(status_text, sizeof(status_text),
                 "Account check HTTP error: %lu",
                 (unsigned long)http_status);

        app_state = 3;
        return;
    }

    name[0] = '\0';

    if (!json_string(
            response, "global_name",
            name, sizeof(name)) &&
        !json_string(
            response, "username",
            name, sizeof(name))) {

        snprintf(name, sizeof(name),
                 "Fluxer account");
    }

    snprintf(account_name, sizeof(account_name),
             "%s", name);

    snprintf(status_text, sizeof(status_text),
             "Your Fluxer session is verified.");

    app_state = 2;
}

static void submit_password_login(void)
{
    char login[INPUT_MAX];
    char password[INPUT_MAX];

    char escaped_login[INPUT_MAX * 2];
    char escaped_password[INPUT_MAX * 2];

    char response[HTTP_BODY_MAX];
    char detail[128];

    u32 http_status = 0;

    /*
       The JSON body is aligned for libctru's
       raw POST data function.
    */
    char body[INPUT_MAX * 4 + 128]
        __attribute__((aligned(4)));

    memset(login, 0, sizeof(login));
    memset(password, 0, sizeof(password));
    memset(body, 0, sizeof(body));

    snprintf(status_text, sizeof(status_text),
             "Enter your Fluxer email.");

    if (!keyboard_input(
            "Email address or username",
            login, sizeof(login), 0)) {
        snprintf(status_text, sizeof(status_text),
                 "Sign-in cancelled.");
        return;
    }

    snprintf(status_text, sizeof(status_text),
             "Enter your Fluxer password.");

    if (!keyboard_input(
            "Password",
            password, sizeof(password), 1)) {
        memset(login, 0, sizeof(login));

        snprintf(status_text, sizeof(status_text),
                 "Sign-in cancelled.");
        return;
    }

    if (!json_escape(
            login, escaped_login,
            sizeof(escaped_login)) ||
        !json_escape(
            password, escaped_password,
            sizeof(escaped_password))) {

        snprintf(status_text, sizeof(status_text),
                 "Could not prepare login data.");
        app_state = 3;
        goto cleanup;
    }

    snprintf(body, sizeof(body),
             "{\"login\":\"%s\",\"password\":\"%s\"}",
             escaped_login, escaped_password);

    snprintf(status_text, sizeof(status_text),
             "Contacting Fluxer securely...");

    Result rc = api_request(
        HTTPC_METHOD_POST,
        "/auth/login",
        body,
        NULL,
        NULL,
        response,
        sizeof(response),
        &http_status,
        detail,
        sizeof(detail)
    );

    if (R_FAILED(rc)) {
        snprintf(status_text, sizeof(status_text),
                 "Login request failed: %.110s", detail);
        app_state = 3;
        goto cleanup;
    }

    if (http_status == 400) {
        char error_code[96];
        char captcha_token[4096];
        error_code[0] = '\0';
        captcha_token[0] = '\0';
        json_string(response, "code", error_code, sizeof(error_code));

        if (strcmp(error_code, "CAPTCHA_REQUIRED") == 0 ||
            strcmp(error_code, "INVALID_CAPTCHA") == 0) {
            snprintf(status_text, sizeof(status_text),
                     "Solving Fluxer security check...");
            if (!altcha_solve(response, captcha_token, sizeof(captcha_token))) {
                snprintf(status_text, sizeof(status_text),
                         "Could not solve this CAPTCHA challenge.");
                app_state = 3;
                memset(captcha_token, 0, sizeof(captcha_token));
                goto cleanup;
            }

            rc = api_request(
                HTTPC_METHOD_POST, "/auth/login", body, NULL,
                captcha_token, response, sizeof(response),
                &http_status, detail, sizeof(detail));

            memset(captcha_token, 0, sizeof(captcha_token));
            if (R_FAILED(rc)) {
                snprintf(status_text, sizeof(status_text),
                         "CAPTCHA retry failed: %.90s", detail);
                app_state = 3;
                goto cleanup;
            }
        }
    }

    if (http_status != 200) {
        show_api_error(http_status, response);
        goto cleanup;
    }

    int result = accept_auth_response(response);

    if (result == 1) {
        verify_session();
    }
    else if (result == 2) {
        /* MFA screen will ask for the code. */
    }
    else {
        snprintf(status_text, sizeof(status_text),
                 "Unexpected response from Fluxer.");
        app_state = 3;
    }

cleanup:
    memset(login, 0, sizeof(login));
    memset(password, 0, sizeof(password));
    memset(escaped_login, 0, sizeof(escaped_login));
    memset(escaped_password, 0, sizeof(escaped_password));
    memset(body, 0, sizeof(body));
}

static void submit_mfa_code(void)
{
    char code[128];
    char response[HTTP_BODY_MAX];
    char detail[128];

    char body[512]
        __attribute__((aligned(4)));

    u32 http_status = 0;

    memset(code, 0, sizeof(code));
    memset(body, 0, sizeof(body));

    if (!keyboard_input(
            "Authenticator code or backup code",
            code, sizeof(code), 0)) {
        snprintf(status_text, sizeof(status_text),
                 "MFA entry cancelled.");
        return;
    }

    char escaped_code[256];

    if (!json_escape(
            code, escaped_code,
            sizeof(escaped_code))) {
        snprintf(status_text, sizeof(status_text),
                 "Could not prepare MFA request.");
        goto cleanup;
    }

    snprintf(body, sizeof(body),
             "{\"ticket\":\"%s\",\"code\":\"%s\"}",
             mfa_ticket, escaped_code);

    snprintf(status_text, sizeof(status_text),
             "Checking authenticator code...");

    Result rc = api_request(
        HTTPC_METHOD_POST,
        "/auth/login/mfa/totp",
        body,
        NULL,
        NULL,
        response,
        sizeof(response),
        &http_status,
        detail,
        sizeof(detail)
    );

    if (R_FAILED(rc)) {
        snprintf(status_text, sizeof(status_text),
                 "MFA request failed: %.100s", detail);
        app_state = 3;
        goto cleanup;
    }

    if (http_status != 200) {
        show_api_error(http_status, response);
        goto cleanup;
    }

    int result = accept_auth_response(response);

    if (result == 1) {
        memset(mfa_ticket, 0, sizeof(mfa_ticket));
        verify_session();
    }
    else {
        snprintf(status_text, sizeof(status_text),
                 "MFA response was not recognized.");
        app_state = 3;
    }

cleanup:
    memset(code, 0, sizeof(code));
    memset(escaped_code, 0, sizeof(escaped_code));
    memset(body, 0, sizeof(body));
}

/* ---------------------------------------------------------
   GRAPHICS
   --------------------------------------------------------- */

static void draw_text(
    const char *value,
    float x, float y,
    float scale, u32 color)
{
    C2D_Text text;

    C2D_TextFontParse(
        &text, ui_font, text_buffer, value);

    C2D_TextOptimize(&text);

    C2D_DrawText(
        &text,
        C2D_WithColor,
        x, y, 0.5f,
        scale, scale,
        color
    );
}

static void draw_button(
    float x, float y,
    float w, float h,
    const char *label,
    u32 color)
{
    C2D_DrawRectSolid(
        x, y, 0.5f, w, h, color);

    draw_text(
        label, x + 12, y + 13,
        0.48f, COL_TEXT);
}

static void render_bottom(void)
{
    C2D_DrawRectSolid(
        0, 0, 0.5f, 320, 34, COL_PANEL);

    draw_text("fluxer", 14, 7, 0.68f, COL_TEXT);
    draw_text("3DS", 265, 9, 0.40f, COL_MUTED);

    if (app_state == 0 || app_state == 3) {
        C2D_DrawRectSolid(
            10, 43, 0.5f, 300, 130, COL_PANEL);

        draw_text(
            "Welcome to Fluxer",
            22, 53, 0.52f, COL_TEXT);

        draw_text(
            "Sign in to your real account.",
            22, 82, 0.36f, COL_MUTED);

        draw_text(
            status_text,
            22, 113, 0.27f,
            app_state == 3 ? COL_RED : COL_MUTED);

        draw_button(
            10, 185, 300, 45,
            "Sign in with email and password",
            COL_ACCENT);
    }
    else if (app_state == 1) {
        C2D_DrawRectSolid(
            10, 43, 0.5f, 300, 130, COL_PANEL);

        draw_text(
            "Two-factor authentication",
            22, 53, 0.42f, COL_TEXT);

        draw_text(
            "Use your authenticator app",
            22, 82, 0.34f, COL_MUTED);

        draw_text(
            status_text,
            22, 120, 0.28f, COL_GREEN);

        draw_button(
            10, 185, 300, 45,
            "Enter verification code",
            COL_ACCENT);
    }
    else if (app_state == 2) {
        C2D_DrawRectSolid(
            10, 43, 0.5f, 300, 130, COL_PANEL);

        draw_text(
            "You're signed in!",
            22, 54, 0.54f, COL_GREEN);

        draw_text(
            account_name,
            22, 91, 0.46f, COL_TEXT);

        draw_text(
            status_text,
            22, 127, 0.30f, COL_MUTED);

        draw_button(
            10, 185, 300, 45,
            "Sign out locally",
            COL_PANEL2);
    }
}

static void render_top(void)
{
    draw_text(
        "FLUXER 3DS",
        22, 22, 0.8f, COL_TEXT);

    draw_text(
        "A lightweight third-party client",
        22, 58, 0.42f, COL_MUTED);

    C2D_DrawRectSolid(
        22, 92, 0.5f, 356, 2, COL_PANEL2);

    draw_text(
        "Milestone 1: account sign-in",
        22, 112, 0.48f, COL_TEXT);

    draw_text(
        "Official Fluxer API  |  HTTPS",
        22, 145, 0.38f, COL_MUTED);

    draw_text(
        "No calls. No video. No background sync.",
        22, 177, 0.34f, COL_MUTED);
}

/* ---------------------------------------------------------
   MAIN
   --------------------------------------------------------- */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    gfxInitDefault();

    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    C3D_RenderTarget *top =
        C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);

    C3D_RenderTarget *bottom =
        C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    ui_font = C2D_FontLoadSystem(CFG_REGION_USA);
    text_buffer = C2D_TextBufNew(4096);

    Result network_rc = init_network();

    if (R_FAILED(network_rc)) {
        snprintf(status_text, sizeof(status_text),
                 "Network init failed: 0x%08lX",
                 (unsigned long)network_rc);

        app_state = 3;
    }

    while (aptMainLoop()) {
        hidScanInput();

        u32 down = hidKeysDown();

        touchPosition touch;
        int touched = 0;

        if (down & KEY_TOUCH) {
            hidTouchRead(&touch);

            touched =
                touch.px >= 10 &&
                touch.px <= 310 &&
                touch.py >= 185 &&
                touch.py <= 230;
        }

        if (down & KEY_START)
            break;

        if ((app_state == 0 || app_state == 3) &&
            ((down & KEY_A) || touched)) {

            submit_password_login();
        }
        else if (app_state == 1 &&
                 ((down & KEY_A) || touched)) {

            submit_mfa_code();
        }
        else if (app_state == 2 &&
                 ((down & KEY_A) || touched)) {

            memset(session_token, 0,
                   sizeof(session_token));

            memset(mfa_ticket, 0,
                   sizeof(mfa_ticket));

            memset(account_name, 0,
                   sizeof(account_name));

            snprintf(account_name,
                     sizeof(account_name),
                     "Not signed in");

            snprintf(status_text,
                     sizeof(status_text),
                     "Signed out locally.");

            app_state = 0;
        }

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

        C2D_TextBufClear(text_buffer);

        C2D_TargetClear(top, COL_BG);
        C2D_SceneBegin(top);
        render_top();

        C2D_TargetClear(bottom, COL_BG);
        C2D_SceneBegin(bottom);
        render_bottom();

        C3D_FrameEnd(0);
    }

    memset(session_token, 0, sizeof(session_token));
    memset(mfa_ticket, 0, sizeof(mfa_ticket));

    cleanup_network();

    C2D_TextBufDelete(text_buffer);
    C2D_FontFree(ui_font);

    C2D_Fini();
    C3D_Fini();
    gfxExit();

    return 0;
}
