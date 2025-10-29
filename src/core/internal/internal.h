/**
 * internal.h - Internal shared definitions for httpmorph modules
 *
 * This header contains common structures, types, and includes used across
 * all httpmorph modules. It should NOT be exposed to external users.
 */

#ifndef INTERNAL_H
#define INTERNAL_H

/* Platform-specific feature macros */
#ifndef _WIN32
    #define _POSIX_C_SOURCE 200809L
    #define _XOPEN_SOURCE 700
#endif

#ifdef _WIN32
    #define _CRT_SECURE_NO_WARNINGS
    #define strcasecmp _stricmp
    #define strncasecmp _strnicmp
    #define strdup _strdup
    #define close closesocket
    #define SNPRINTF_SIZE(size) ((int)(size))
    #define SELECT_NFDS(sockfd) 0
#else
    #define SNPRINTF_SIZE(size) (size)
    #define SELECT_NFDS(sockfd) ((sockfd) + 1)
#endif

/* Standard library includes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>

/* Platform-specific headers */
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    typedef int socklen_t;
#else
    #include <strings.h>
    #include <unistd.h>
    #include <errno.h>
    #include <fcntl.h>
    #include <sys/socket.h>
    #include <sys/select.h>
    #include <netdb.h>
    #include <arpa/inet.h>
    #include <netinet/tcp.h>
#endif

/* BoringSSL/OpenSSL includes */
#ifdef _WIN32
#include "../../include/boringssl_compat.h"
#endif
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/evp.h>

/* HTTP/2 support */
#ifdef HAVE_NGHTTP2
#include <nghttp2/nghttp2.h>
#endif

/* httpmorph public API */
#include "../../include/httpmorph.h"

/* Internal modules */
#include "../../tls/browser_profiles.h"
#include "../io_engine.h"
#include "../connection_pool.h"

/* ==================================================================
 * INTERNAL STRUCTURES
 * ================================================================== */

/* Forward declare buffer pool */
typedef struct httpmorph_buffer_pool httpmorph_buffer_pool_t;

/**
 * HTTP client structure
 */
struct httpmorph_client {
    SSL_CTX *ssl_ctx;
    io_engine_t *io_engine;
    httpmorph_pool_t *pool;
    httpmorph_buffer_pool_t *buffer_pool;  /* Buffer pool for response bodies */

    /* Configuration */
    uint32_t timeout_ms;
    bool follow_redirects;
    uint32_t max_redirects;

    /* Browser fingerprint */
    const browser_profile_t *browser_profile;
};

/**
 * Cookie structure
 */
typedef struct cookie {
    char *name;
    char *value;
    char *domain;
    char *path;
    time_t expires;  /* 0 = session cookie */
    bool secure;
    bool http_only;
    struct cookie *next;
} cookie_t;

/**
 * Session structure
 */
struct httpmorph_session {
    httpmorph_client_t *client;
    const browser_profile_t *browser_profile;

    /* Connection pool for this session */
    httpmorph_pool_t *pool;

    /* Cookie jar */
    cookie_t *cookies;
    size_t cookie_count;

    /* HTTP/2 session */
#ifdef HAVE_NGHTTP2
    nghttp2_session *http2_session;
#endif
};

/**
 * Connection structure (internal use only)
 */
typedef struct {
    int sockfd;
    SSL *ssl;
    bool connected;
    bool tls_established;

    char *host;
    uint16_t port;

    /* HTTP/2 state */
    bool http2_enabled;
#ifdef HAVE_NGHTTP2
    nghttp2_session *session;
#endif

    /* Timing */
    uint64_t connect_time_us;
    uint64_t tls_time_us;

    /* Last used timestamp for pool management */
    time_t last_used;
} connection_t;

/* ==================================================================
 * WINDOWS COMPATIBILITY
 * ================================================================== */

#ifdef _WIN32
/* strnlen is not available in older MSVC versions */
#if _MSC_VER < 1900  /* Before Visual Studio 2015 */
static inline size_t strnlen(const char *s, size_t n) {
    const char *p = (const char *)memchr(s, 0, n);
    return p ? (size_t)(p - s) : n;
}
#endif

/* strndup is not available on Windows MSVC */
static inline char* strndup(const char *s, size_t n) {
    size_t len = strnlen(s, n);
    char *dup = (char*)malloc(len + 1);
    if (dup) {
        memcpy(dup, s, len);
        dup[len] = '\0';
    }
    return dup;
}
#endif

#endif /* INTERNAL_H */
