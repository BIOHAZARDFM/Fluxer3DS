#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#define API_BASE       "https://api.fluxer.app/v1"
#define SOC_ALIGN      0x1000
#define SOC_BUFFERSIZE 0x100000
#define HTTP_BODY_MAX  16384
#define TOKEN_MAX      2048
#define CODE_MAX       64
#define SECRET_MAX     256

static u32 *soc_buffer;
static int network_ready;
static char session_token[TOKEN_MAX];
static char handoff_code[CODE_MAX];
static char poll_secret[SECRET_MAX];
static char account_name[96] = "Not signed in";
static char status_text[160] = "Connect to the internet, then begin sign-in.";
static int app_state = 0; /* 0 = welcome, 1 = waiting for approval, 2 = signed in, 3 = error */
static C2D_Font ui_font;
static C2D_TextBuf text_buffer;

#define COL_BG       C2D_Color32(29, 31, 36, 255)
#define COL_PANEL    C2D_Color32(39, 42, 49, 255)
#define COL_PANEL2   C2D_Color32(49, 53, 62, 255)
#define COL_ACCENT   C2D_Color32(115, 83, 220, 255)
#define COL_TEXT     C2D_Color32(245, 246, 248, 255)
#define COL_MUTED    C2D_Color32(173, 178, 190, 255)
#define COL_GREEN    C2D_Color32(88, 190, 133, 255)
#define COL_RED      C2D_Color32(225, 104, 104, 255)

static Result init_network(void)
{
    Result rc;
    soc_buffer = (u32 *)memalign(SOC_ALIGN, SOC_BUFFERSIZE);
    if (!soc_buffer) return -1;

    rc = socInit(soc_buffer, SOC_BUFFERSIZE);
    if (R_FAILED(rc)) {
        free(soc_buffer);
        soc_buffer = NULL;
        return rc;
    }

    /* libctru's HTTP service performs HTTPS and certificate validation. */
    rc = httpcInit(0);
    if (R_FAILED(rc)) {
        socExit();
        free(soc_buffer);
        soc_buffer = NULL;
        return rc;
    }
    network_ready = 1;
    return 0;
}

static void cleanup_network(void)
{
    if (network_ready) {
        httpcExit();
        network_ready = 0;
    }
    if (soc_buffer) {
        socExit();
        free(soc_buffer);
        soc_buffer = NULL;
    }
}

/* Small JSON string reader for the simple fields used in this first milestone.
   It handles escaped quotes and backslashes; it is not a general JSON parser. */
static int json_string(const char *json, const char *key, char *out, size_t out_size)
{
    char needle[80];
    const char *p;
    size_t n = 0;

    if (!json || !key || !out || out_size < 2) return 0;
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    p = strstr(json, needle);
    if (!p) return 0;
    p = strchr(p + strlen(needle), ':');
    if (!p) return 0;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '"') return 0;
    p++;

    while (*p && *p != '"' && n + 1 < out_size) {
        if (*p == '\\' && p[1]) {
            p++;
            if (*p == 'n') out[n++] = '\n';
            else if (*p == 'r') out[n++] = '\r';
            else if (*p == 't') out[n++] = '\t';
            else out[n++] = *p;
            p++;
        } else {
            out[n++] = *p++;
        }
    }
    out[n] = '\0';
    return (*p == '"');
}

static Result add_trust_roots(httpcContext *ctx)
{
    Result rc;
    /* These are built-in Nintendo SSL trust anchors. Verification remains ON.
       If Fluxer's current certificate chain uses a newer root, the request may
       fail safely until a suitable root is added to the 3DS trust store. */
    rc = httpcAddDefaultCert(ctx, SSLC_DefaultRootCert_USERTrust);
    if (R_FAILED(rc)) return rc;
    rc = httpcAddDefaultCert(ctx, SSLC_DefaultRootCert_COMODO);
    if (R_FAILED(rc)) return rc;
    rc = httpcAddDefaultCert(ctx, SSLC_DefaultRootCert_DigiCert_EV);
    return rc;
}

static Result api_request(HTTPC_RequestMethod method, const char *path,
                          const char *body, const char *auth_token,
                          char *response, size_t response_size, u32 *http_status)
{
    httpcContext ctx;
    char url[512];
    Result rc;
    u32 downloaded = 0;
    u32 status = 0;

    if (!path || !response || response_size < 2) return -1;
    snprintf(url, sizeof(url), "%s%s", API_BASE, path);
    response[0] = '\0';

    rc = httpcOpenContext(&ctx, method, url, 1);
    if (R_FAILED(rc)) return rc;

    rc = httpcSetSSLOpt(&ctx, SSLCOPT_Default);
    if (R_FAILED(rc)) goto done;
    rc = add_trust_roots(&ctx);
    if (R_FAILED(rc)) goto done;

    rc = httpcAddRequestHeaderField(&ctx, "Accept", "application/json");
    if (R_FAILED(rc)) goto done;
    rc = httpcAddRequestHeaderField(&ctx, "User-Agent", "Fluxer3DS/0.1");
    if (R_FAILED(rc)) goto done;

    if (auth_token && auth_token[0]) {
        char auth_header[TOKEN_MAX + 16];
        snprintf(auth_header, sizeof(auth_header), "Bearer %s", auth_token);
        rc = httpcAddRequestHeaderField(&ctx, "Authorization", auth_header);
        if (R_FAILED(rc)) goto done;
    }

    if (body) {
        rc = httpcAddRequestHeaderField(&ctx, "Content-Type", "application/json");
        if (R_FAILED(rc)) goto done;
        rc = httpcAddPostDataRaw(&ctx, (const u32 *)body, (u32)strlen(body));
        if (R_FAILED(rc)) goto done;
    }

    rc = httpcBeginRequest(&ctx);
    if (R_FAILED(rc)) goto done;

    rc = httpcGetResponseStatusCode(&ctx, &status);
    if (R_FAILED(rc)) goto done;
    if (http_status) *http_status = status;

    if (status != 204) {
        rc = httpcDownloadData(&ctx, (u8 *)response, (u32)(response_size - 1), &downloaded);
        if (R_FAILED(rc) && rc != (Result)HTTPC_RESULTCODE_DOWNLOADPENDING) goto done;
        if (downloaded >= response_size) downloaded = (u32)response_size - 1;
        response[downloaded] = '\0';
    } else {
        response[0] = '\0';
    }
    rc = 0;

done:
    httpcCloseContext(&ctx);
    return rc;
}

static int begin_handoff(void)
{
    char response[HTTP_BODY_MAX];
    if (!network_ready) {
        snprintf(status_text, sizeof(status_text), "Network is unavailable. Restart after checking Wi-Fi.");
        app_state = 3;
        return 0;
    }
    u32 http_status = 0;
    Result rc = api_request(HTTPC_METHOD_POST, "/auth/handoff/initiate", "{}",
                            NULL, response, sizeof(response), &http_status);
    if (R_FAILED(rc)) {
        snprintf(status_text, sizeof(status_text), "HTTPS request failed (0x%08lX).", (unsigned long)rc);
        app_state = 3;
        return 0;
    }
    if (http_status != 200) {
        snprintf(status_text, sizeof(status_text), "Fluxer returned HTTP %lu.", (unsigned long)http_status);
        app_state = 3;
        return 0;
    }
    if (!json_string(response, "code", handoff_code, sizeof(handoff_code)) ||
        !json_string(response, "poll_secret", poll_secret, sizeof(poll_secret))) {
        snprintf(status_text, sizeof(status_text), "Fluxer response was missing handoff details.");
        app_state = 3;
        return 0;
    }

    app_state = 1;
    snprintf(status_text, sizeof(status_text), "Approve this sign-in from a device already signed in to Fluxer.");
    return 1;
}

static int poll_handoff(void)
{
    char path[160];
    char body[384];
    char response[HTTP_BODY_MAX];
    char state[32];
    u32 http_status = 0;
    Result rc;

    snprintf(path, sizeof(path), "/auth/handoff/%s/status", handoff_code);
    snprintf(body, sizeof(body), "{\"poll_secret\":\"%s\"}", poll_secret);

    rc = api_request(HTTPC_METHOD_POST, path, body, NULL,
                     response, sizeof(response), &http_status);
    if (R_FAILED(rc) || http_status != 200) return 0;

    if (!json_string(response, "status", state, sizeof(state))) return 0;

    if (strcmp(state, "denied") == 0) {
        snprintf(status_text, sizeof(status_text), "The sign-in request was declined.");
        app_state = 3;
        return -1;
    }
    if (strcmp(state, "expired") == 0) {
        snprintf(status_text, sizeof(status_text), "The sign-in code expired. Start again.");
        app_state = 3;
        return -1;
    }
    if (strcmp(state, "completed") == 0) {
        if (!json_string(response, "token", session_token, sizeof(session_token))) {
            snprintf(status_text, sizeof(status_text), "Fluxer approved sign-in but no token was returned.");
            app_state = 3;
            return -1;
        }
        return 1;
    }
    return 0;
}

static void verify_session(void)
{
    char response[HTTP_BODY_MAX];
    char name[96];
    u32 http_status = 0;
    Result rc = api_request(HTTPC_METHOD_GET, "/users/@me", NULL,
                            session_token, response, sizeof(response), &http_status);

    if (R_FAILED(rc) || http_status != 200) {
        memset(session_token, 0, sizeof(session_token));
        snprintf(status_text, sizeof(status_text), "Sign-in received, but account verification failed.");
        app_state = 3;
        return;
    }

    if (!json_string(response, "global_name", name, sizeof(name)) &&
        !json_string(response, "username", name, sizeof(name))) {
        snprintf(name, sizeof(name), "Fluxer account");
    }
    snprintf(account_name, sizeof(account_name), "%s", name);
    snprintf(status_text, sizeof(status_text), "Account session verified. Next: load your chats.");
    app_state = 2;
}

static void draw_text(const char *value, float x, float y, float scale, u32 color)
{
    C2D_Text text;
    C2D_TextFontParse(&text, ui_font, text_buffer, value);
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_WithColor, x, y, 0.5f, scale, scale, color);
}

static void draw_button(float x, float y, float w, float h, const char *label, u32 color)
{
    C2D_DrawRectSolid(x, y, 0.5f, w, h, color);
    draw_text(label, x + 12, y + 13, 0.48f, COL_TEXT);
}

static void render_bottom(void)
{
    C2D_DrawRectSolid(0, 0, 0.5f, 320, 34, COL_PANEL);
    draw_text("fluxer", 14, 7, 0.68f, COL_TEXT);
    draw_text("3DS", 265, 9, 0.40f, COL_MUTED);

    if (app_state == 0 || app_state == 3) {
        C2D_DrawRectSolid(10, 43, 0.5f, 300, 130, COL_PANEL);
        draw_text("Welcome to Fluxer", 22, 53, 0.52f, COL_TEXT);
        draw_text("Sign in to your real account.", 22, 82, 0.36f, COL_MUTED);
        draw_text(status_text, 22, 113, 0.30f, app_state == 3 ? COL_RED : COL_MUTED);
        draw_button(10, 185, 300, 45, "Start secure device sign-in", COL_ACCENT);
    } else if (app_state == 1) {
        C2D_DrawRectSolid(10, 43, 0.5f, 300, 130, COL_PANEL);
        draw_text("Approve this device", 22, 52, 0.50f, COL_TEXT);
        draw_text("Enter this code on your signed-in", 22, 81, 0.32f, COL_MUTED);
        draw_text("Fluxer device:", 22, 101, 0.32f, COL_MUTED);
        draw_text(handoff_code, 22, 126, 0.68f, COL_GREEN);
        draw_text("Waiting for approval...", 22, 157, 0.30f, COL_MUTED);
        draw_button(10, 185, 300, 45, "Cancel sign-in", COL_PANEL2);
    } else if (app_state == 2) {
        C2D_DrawRectSolid(10, 43, 0.5f, 300, 130, COL_PANEL);
        draw_text("You're signed in", 22, 54, 0.54f, COL_GREEN);
        draw_text(account_name, 22, 91, 0.46f, COL_TEXT);
        draw_text(status_text, 22, 127, 0.30f, COL_MUTED);
        draw_button(10, 185, 300, 45, "Sign out locally", COL_PANEL2);
    }
}

static void render_top(void)
{
    draw_text("FLUXER 3DS", 22, 22, 0.8f, COL_TEXT);
    draw_text("A lightweight third-party client", 22, 58, 0.42f, COL_MUTED);
    C2D_DrawRectSolid(22, 92, 0.5f, 356, 2, COL_PANEL2);
    draw_text("Milestone 1: secure sign-in", 22, 112, 0.48f, COL_TEXT);
    draw_text("Real Fluxer API  |  HTTPS", 22, 145, 0.38f, COL_MUTED);
    draw_text("No calls. No video. No background sync.", 22, 177, 0.34f, COL_MUTED);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget *bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    ui_font = C2D_FontLoadSystem(CFG_REGION_USA);
    text_buffer = C2D_TextBufNew(4096);

    if (R_FAILED(init_network())) {
        snprintf(status_text, sizeof(status_text), "Network init failed. Check Wi-Fi and retry.");
        app_state = 3;
    }

    unsigned int frame_counter = 0;
    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();
        touchPosition touch;
        int touched = 0;
        if (down & KEY_TOUCH) {
            hidTouchRead(&touch);
            touched = (touch.px >= 10 && touch.px <= 310 && touch.py >= 185 && touch.py <= 230);
        }

        if (down & KEY_START) break;

        if ((app_state == 0 || app_state == 3) &&
            (down & KEY_A || touched)) {
            if (app_state == 3) {
                memset(handoff_code, 0, sizeof(handoff_code));
                memset(poll_secret, 0, sizeof(poll_secret));
            }
            begin_handoff();
        } else if (app_state == 1 && (down & KEY_B || touched)) {
            memset(handoff_code, 0, sizeof(handoff_code));
            memset(poll_secret, 0, sizeof(poll_secret));
            app_state = 0;
            snprintf(status_text, sizeof(status_text), "Sign-in cancelled.");
        } else if (app_state == 2 && (down & KEY_A || touched)) {
            memset(session_token, 0, sizeof(session_token));
            memset(account_name, 0, sizeof(account_name));
            snprintf(account_name, sizeof(account_name), "Not signed in");
            app_state = 0;
            snprintf(status_text, sizeof(status_text), "Signed out locally. The Fluxer session remains active.");
        }

        if (app_state == 1 && (++frame_counter % 180) == 0) {
            int poll_result = poll_handoff();
            if (poll_result == 1) verify_session();
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
    memset(poll_secret, 0, sizeof(poll_secret));
    cleanup_network();
    C2D_TextBufDelete(text_buffer);
    C2D_FontFree(ui_font);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
