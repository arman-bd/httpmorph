/**
 * string_intern.c - String interning implementation
 */

#include "string_intern.h"
#include <string.h>
#ifndef _WIN32
    #include <strings.h>
#endif

/* Common HTTP header names (case-insensitive) */
static const char* COMMON_HEADERS[] = {
    /* Request headers */
    "Accept",
    "Accept-Encoding",
    "Accept-Language",
    "Authorization",
    "Cache-Control",
    "Connection",
    "Content-Length",
    "Content-Type",
    "Cookie",
    "Host",
    "If-Modified-Since",
    "If-None-Match",
    "Origin",
    "Referer",
    "User-Agent",

    /* Response headers */
    "Age",
    "Content-Encoding",
    "Date",
    "ETag",
    "Expires",
    "Last-Modified",
    "Location",
    "Server",
    "Set-Cookie",
    "Transfer-Encoding",
    "Vary",

    /* Common custom headers */
    "X-Forwarded-For",
    "X-Forwarded-Proto",
    "X-Real-IP",

    NULL  /* Sentinel */
};

/* Total number of interned strings */
#define NUM_INTERNED_STRINGS (sizeof(COMMON_HEADERS) / sizeof(char*) - 1)

/**
 * Get an interned string for a common header name
 */
const char* string_intern_get(const char *str, size_t len) {
    if (!str || len == 0) {
        return NULL;
    }

    /* Linear search is fine for ~30 strings (very cache-friendly) */
    for (size_t i = 0; COMMON_HEADERS[i] != NULL; i++) {
        size_t intern_len = strlen(COMMON_HEADERS[i]);

        /* Fast length check first */
        if (len != intern_len) {
            continue;
        }

        /* Case-insensitive comparison */
        if (strncasecmp(str, COMMON_HEADERS[i], len) == 0) {
            return COMMON_HEADERS[i];
        }
    }

    return NULL;  /* Not a common header */
}

/**
 * Check if a string is interned
 */
int string_intern_is_interned(const char *str) {
    if (!str) {
        return 0;
    }

    /* Check if pointer is in our interned array */
    for (size_t i = 0; COMMON_HEADERS[i] != NULL; i++) {
        if (str == COMMON_HEADERS[i]) {
            return 1;
        }
    }

    return 0;
}
