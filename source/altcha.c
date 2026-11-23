#include "altcha.h"
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>

#define ALTCHA_MAX_CHALLENGE 3072
#define ALTCHA_MAX_KEY 64

static const char *jvalue(const char *json, const char *key) {
    char needle[80];
    const char *p;
    if (!json || !key) return NULL;
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    p = strstr(json, needle);
    if (!p) return NULL;
    p = strchr(p + strlen(needle), ':');
    if (!p) return NULL;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}

static int jstring(const char *json, const char *key, char *out, size_t cap) {
    const char *p = jvalue(json, key);
    size_t n = 0;
    if (!p || *p != '"' || cap < 2) return 0;
    p++;
    while (*p && *p != '"' && n + 1 < cap) {
        if (*p == '\\' && p[1]) {
            p++;
            if (*p == 'n') out[n++] = '\n';
            else if (*p == 'r') out[n++] = '\r';
            else if (*p == 't') out[n++] = '\t';
            else out[n++] = *p;
            p++;
        } else out[n++] = *p++;
    }
    out[n] = 0;
    return *p == '"';
}

static int jint(const char *json, const char *key, unsigned long *out) {
    const char *p = jvalue(json, key);
    char *end;
    unsigned long v;
    if (!p || *p < '0' || *p > '9') return 0;
    v = strtoul(p, &end, 10);
    if (end == p || (*end != ',' && *end != '}' && *end != ' ' && *end != '\n')) return 0;
    *out = v;
    return 1;
}

static int jobject(const char *json, const char *key, char *out, size_t cap) {
    const char *p = jvalue(json, key), *start;
    int depth = 0, in_string = 0, escaped = 0;
    size_t len;
    if (!p || *p != '{') return 0;
    start = p;
    for (; *p; ++p) {
        if (in_string) {
            if (escaped) escaped = 0;
            else if (*p == '\\') escaped = 1;
            else if (*p == '"') in_string = 0;
        } else {
            if (*p == '"') in_string = 1;
            else if (*p == '{') depth++;
            else if (*p == '}') {
                depth--;
                if (depth == 0) {
                    len = (size_t)(p - start + 1);
                    if (len + 1 > cap) return 0;
                    memcpy(out, start, len);
                    out[len] = 0;
                    return 1;
                }
            }
        }
    }
    return 0;
}

static int hex_decode(const char *hex, unsigned char *out, size_t cap, size_t *out_len) {
    size_t n, i;
    if (!hex) return 0;
    n = strlen(hex);
    if ((n & 1) || n / 2 > cap) return 0;
    for (i = 0; i < n; i += 2) {
        unsigned int v;
        if (sscanf(hex + i, "%2x", &v) != 1) return 0;
        out[i / 2] = (unsigned char)v;
    }
    *out_len = n / 2;
    return 1;
}

static void hex_encode(const unsigned char *in, size_t len, char *out) {
    static const char h[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < len; ++i) {
        out[i * 2] = h[in[i] >> 4];
        out[i * 2 + 1] = h[in[i] & 15];
    }
    out[len * 2] = 0;
}

static int has_prefix(const char *hex, const char *prefix) {
    size_t n = strlen(prefix);
    return n <= strlen(hex) && strncasecmp(hex, prefix, n) == 0;
}

static int b64_encode(const unsigned char *src, size_t len, char *dst, size_t cap) {
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i = 0, o = 0;
    while (i < len) {
        uint32_t v = (uint32_t)src[i++] << 16;
        int have2 = i < len, have3;
        if (have2) v |= (uint32_t)src[i++] << 8;
        have3 = i < len;
        if (have3) v |= src[i++];
        if (o + 4 >= cap) return 0;
        dst[o++] = t[(v >> 18) & 63];
        dst[o++] = t[(v >> 12) & 63];
        dst[o++] = have2 ? t[(v >> 6) & 63] : '=';
        dst[o++] = have3 ? t[v & 63] : '=';
    }
    dst[o] = 0;
    return 1;
}

int altcha_solve(const char *error_json, char *token, size_t token_size) {
    char challenge[ALTCHA_MAX_CHALLENGE];
    char params[2048];
    char algorithm[64], nonce_hex[256], salt_hex[256], prefix[128], signature[512];
    char derived_hex[ALTCHA_MAX_KEY * 2 + 1];
    char payload[ALTCHA_MAX_CHALLENGE + 512];
    unsigned char nonce[128], salt[128], password[132], derived[ALTCHA_MAX_KEY];
    size_t nonce_len = 0, salt_len = 0, key_len = 32;
    unsigned long cost = 0, counter;
    const mbedtls_md_info_t *md;
    u64 start;
    int rc = 0;

    if (!error_json || !token || token_size < 32) return 0;
    if (!jobject(error_json, "altcha_challenge", challenge, sizeof(challenge))) return 0;
    if (!jobject(challenge, "parameters", params, sizeof(params))) return 0;
    if (!jstring(params, "algorithm", algorithm, sizeof(algorithm)) ||
        strcmp(algorithm, "PBKDF2/SHA-256") != 0 ||
        !jstring(params, "nonce", nonce_hex, sizeof(nonce_hex)) ||
        !jstring(params, "salt", salt_hex, sizeof(salt_hex)) ||
        !jstring(params, "keyPrefix", prefix, sizeof(prefix)) ||
        !jstring(challenge, "signature", signature, sizeof(signature)) ||
        !jint(params, "cost", &cost)) return 0;
    {
        unsigned long k;
        if (jint(params, "keyLength", &k)) key_len = (size_t)k;
    }
    if (key_len == 0 || key_len > ALTCHA_MAX_KEY || cost == 0 || cost > 1000000UL) return 0;
    if (!hex_decode(nonce_hex, nonce, sizeof(nonce), &nonce_len) ||
        !hex_decode(salt_hex, salt, sizeof(salt), &salt_len) ||
        nonce_len == 0 || salt_len == 0 || strlen(prefix) > key_len * 2) return 0;

    md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md) return 0;

    start = osGetTime();
    for (counter = 0; counter <= 0xffffffffUL; ++counter) {
        size_t pass_len = nonce_len + 4;
        memcpy(password, nonce, nonce_len);
        password[nonce_len] = (unsigned char)(counter >> 24);
        password[nonce_len + 1] = (unsigned char)(counter >> 16);
        password[nonce_len + 2] = (unsigned char)(counter >> 8);
        password[nonce_len + 3] = (unsigned char)counter;

        rc = mbedtls_pkcs5_pbkdf2_hmac(md, password, pass_len, salt, salt_len,
                                       (unsigned int)cost, (uint32_t)key_len, derived);
        if (rc != 0) return 0;
        hex_encode(derived, key_len, derived_hex);
        if (has_prefix(derived_hex, prefix)) break;
        if ((counter & 31) == 0 && osGetTime() - start > 85000) return 0;
    }
    if (counter > 0xffffffffUL) return 0;

    if (snprintf(payload, sizeof(payload),
        "{\"challenge\":%s,\"solution\":{\"counter\":%lu,\"derivedKey\":\"%s\"}}",
        challenge, counter, derived_hex) >= (int)sizeof(payload)) return 0;
    if (!b64_encode((const unsigned char *)payload, strlen(payload), token, token_size)) return 0;

    memset(password, 0, sizeof(password));
    memset(derived, 0, sizeof(derived));
    memset(payload, 0, sizeof(payload));
    return 1;
}
