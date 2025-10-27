/**
 * httpmorph.c - Core HTTP/HTTPS client implementation
 *
 * Features:
 * - BoringSSL for TLS with custom fingerprinting
 * - io_uring for high-performance I/O (Linux)
 * - Browser-specific TLS fingerprints
 * - HTTP/2 support with proper SETTINGS frames
 * - Connection pooling
 */

/* Note: windows_compat.h is force-included via /FI compiler flag on Windows */

/* POSIX feature macros (only for POSIX systems) */
#ifndef _WIN32
    #define _POSIX_C_SOURCE 200809L
    #define _XOPEN_SOURCE 700
#endif

/* Windows compatibility macros (MUST come before standard library includes) */
#ifdef _WIN32
    #define _CRT_SECURE_NO_WARNINGS
    #define strcasecmp _stricmp
    #define strncasecmp _strnicmp
    #define strdup _strdup
    #define close closesocket
    /* ssize_t is defined via windows_compat.h as SSIZE_T */
    /* Helper for snprintf size parameter (Windows uses int, POSIX uses size_t) */
    #define SNPRINTF_SIZE(size) ((int)(size))
    /* Windows select() ignores first parameter (nfds) */
    #define SELECT_NFDS(sockfd) 0
#else
    #define SNPRINTF_SIZE(size) (size)
    /* POSIX select() needs nfds = highest fd + 1 */
    #define SELECT_NFDS(sockfd) ((sockfd) + 1)
#endif

#include "../include/httpmorph.h"
#include "../tls/browser_profiles.h"
#include "io_engine.h"
#include "connection_pool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>

/* Platform-specific headers */
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;

    /* strnlen is not available in older MSVC versions */
    #if _MSC_VER < 1900  /* Before Visual Studio 2015 */
        static size_t strnlen(const char *s, size_t n) {
            const char *p = (const char *)memchr(s, 0, n);
            return p ? (size_t)(p - s) : n;
        }
    #endif

    /* strndup is not available on Windows MSVC */
    static char* strndup(const char *s, size_t n) {
        size_t len = strnlen(s, n);
        char *dup = (char*)malloc(len + 1);
        if (dup) {
            memcpy(dup, s, len);
            dup[len] = '\0';
        }
        return dup;
    }
#else
    #include <strings.h>  /* for strcasecmp */
    #include <unistd.h>
    #include <errno.h>
    #include <fcntl.h>
    #include <sys/socket.h>
    #include <sys/select.h>
    #include <netdb.h>
    #include <arpa/inet.h>
    #include <netinet/tcp.h>
#endif

/* BoringSSL includes */
#ifdef _WIN32
/* Include Windows compatibility layer for BoringSSL */
#include "../include/boringssl_compat.h"
#endif

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/evp.h>

/* HTTP/2 support */
#ifdef HAVE_NGHTTP2
#include <nghttp2/nghttp2.h>
#endif

/* Library state */
static bool httpmorph_initialized = false;
static io_engine_t *default_io_engine = NULL;

/* HTTP client structure */
struct httpmorph_client {
    SSL_CTX *ssl_ctx;
    io_engine_t *io_engine;
    httpmorph_pool_t *pool;

    /* Configuration */
    uint32_t timeout_ms;
    bool follow_redirects;
    uint32_t max_redirects;

    /* Browser fingerprint */
    const browser_profile_t *browser_profile;
};

/* Session structure */
/* Cookie structure */
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

/* Response structure is defined in httpmorph.h */

/* Connection structure */
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

/* Helper: Get current time in microseconds */
static uint64_t get_time_us(void) {
#ifdef _WIN32
    LARGE_INTEGER frequency, counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (uint64_t)((counter.QuadPart * 1000000) / frequency.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
#endif
}

/* Helper: Parse URL */
static int parse_url(const char *url, char **scheme, char **host,
                     uint16_t *port, char **path) {
    if (!url || !scheme || !host || !port || !path) {
        return -1;
    }

    /* Simple URL parser */
    const char *p = url;

    /* Parse scheme */
    const char *scheme_end = strstr(p, "://");
    if (!scheme_end) {
        return -1;
    }

    *scheme = strndup(p, scheme_end - p);
    p = scheme_end + 3;

    /* Parse host and optional port */
    const char *path_start = strchr(p, '/');
    const char *port_start = strchr(p, ':');

    if (port_start && (!path_start || port_start < path_start)) {
        /* Port specified */
        *host = strndup(p, port_start - p);
        p = port_start + 1;

        char *end;
        long port_num = strtol(p, &end, 10);
        if (port_num <= 0 || port_num > 65535) {
            free(*host);
            free(*scheme);
            return -1;
        }
        *port = (uint16_t)port_num;
        p = end;
    } else {
        /* Default port */
        *port = (strcmp(*scheme, "https") == 0) ? 443 : 80;

        if (path_start) {
            *host = strndup(p, path_start - p);
            p = path_start;
        } else {
            *host = strdup(p);
            p = p + strlen(p);
        }
    }

    /* Parse path */
    if (*p == '\0') {
        *path = strdup("/");
    } else {
        *path = strdup(p);
    }

    return 0;
}

/* Configure SSL context with browser profile */
static int configure_ssl_ctx(SSL_CTX *ctx, const browser_profile_t *profile) {
    if (!ctx || !profile) {
        return -1;
    }

    /* Set TLS version range */
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);

    /* Build cipher list string from profile */
    char cipher_list[2048] = {0};
    char *p = cipher_list;

    for (int i = 0; i < profile->cipher_suite_count; i++) {
        uint16_t cs = profile->cipher_suites[i];

        /* Map cipher suite code to BoringSSL name */
        const char *name = NULL;
        switch (cs) {
            case 0x1301: name = "TLS_AES_128_GCM_SHA256"; break;
            case 0x1302: name = "TLS_AES_256_GCM_SHA384"; break;
            case 0x1303: name = "TLS_CHACHA20_POLY1305_SHA256"; break;
            case 0xc02b: name = "ECDHE-ECDSA-AES128-GCM-SHA256"; break;
            case 0xc02f: name = "ECDHE-RSA-AES128-GCM-SHA256"; break;
            case 0xc02c: name = "ECDHE-ECDSA-AES256-GCM-SHA384"; break;
            case 0xc030: name = "ECDHE-RSA-AES256-GCM-SHA384"; break;
            case 0xcca9: name = "ECDHE-ECDSA-CHACHA20-POLY1305"; break;
            case 0xcca8: name = "ECDHE-RSA-CHACHA20-POLY1305"; break;
            default: continue;  /* Skip unsupported */
        }

        if (name) {
            if (p != cipher_list) {
                *p++ = ':';
            }
            strcpy(p, name);
            p += strlen(name);
        }
    }

    /* Set cipher list */
    if (SSL_CTX_set_cipher_list(ctx, cipher_list) != 1) {
        return -1;
    }

    /* Set supported curves */
    if (profile->curve_count > 0) {
        int nids[MAX_CURVES];
        int nid_count = 0;

        for (int i = 0; i < profile->curve_count && nid_count < MAX_CURVES; i++) {
            int nid = -1;
            switch (profile->curves[i]) {
                case 0x001d: nid = NID_X25519; break;
                case 0x0017: nid = NID_X9_62_prime256v1; break;  /* secp256r1 */
                case 0x0018: nid = NID_secp384r1; break;
                case 0x0019: nid = NID_secp521r1; break;
                default: continue;
            }

            if (nid != -1) {
                nids[nid_count++] = nid;
            }
        }

        if (nid_count > 0) {
            /* BoringSSL uses SSL_CTX_set1_groups (curves are now called groups) */
            SSL_CTX_set1_groups(ctx, nids, nid_count);
        }
    }

    /* Set ALPN protocols for HTTP/2 support */
    if (profile->alpn_protocol_count > 0) {
        /* Build ALPN protocol list */
        unsigned char alpn_list[256];
        unsigned char *alpn_p = alpn_list;

        for (int i = 0; i < profile->alpn_protocol_count; i++) {
            size_t len = strlen(profile->alpn_protocols[i]);
            if (alpn_p - alpn_list + len + 1 > sizeof(alpn_list)) {
                break;
            }
            *alpn_p++ = (unsigned char)len;
            memcpy(alpn_p, profile->alpn_protocols[i], len);
            alpn_p += len;
        }

        SSL_CTX_set_alpn_protos(ctx, alpn_list, alpn_p - alpn_list);
    }

    return 0;
}

/**
 * Initialize the httpmorph library
 */
int httpmorph_init(void) {
    if (httpmorph_initialized) {
        return HTTPMORPH_OK;
    }

#ifdef _WIN32
    /* Initialize Winsock on Windows */
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", result);
        return HTTPMORPH_ERROR_NETWORK;
    }
#endif

    /* Initialize BoringSSL - no explicit initialization needed */
    /* BoringSSL initializes automatically, unlike old OpenSSL versions */

    /* Create default I/O engine */
    default_io_engine = io_engine_create(256);
    if (!default_io_engine) {
#ifdef _WIN32
        WSACleanup();
#endif
        return HTTPMORPH_ERROR_MEMORY;
    }

    httpmorph_initialized = true;
    return HTTPMORPH_OK;
}

/**
 * Cleanup the httpmorph library
 */
void httpmorph_cleanup(void) {
    if (!httpmorph_initialized) {
        return;
    }

    if (default_io_engine) {
        io_engine_destroy(default_io_engine);
        default_io_engine = NULL;
    }

    ERR_free_strings();
    EVP_cleanup();

#ifdef _WIN32
    /* Cleanup Winsock on Windows */
    WSACleanup();
#endif

    httpmorph_initialized = false;
}

/**
 * Get library version string
 */
const char* httpmorph_version(void) {
    static char version[32];
    snprintf(version, sizeof(version), "%d.%d.%d",
             HTTPMORPH_VERSION_MAJOR,
             HTTPMORPH_VERSION_MINOR,
             HTTPMORPH_VERSION_PATCH);
    return version;
}

/**
 * Create a new HTTP client
 */
httpmorph_client_t* httpmorph_client_create(void) {
    if (!httpmorph_initialized) {
        httpmorph_init();
    }

    httpmorph_client_t *client = calloc(1, sizeof(httpmorph_client_t));
    if (!client) {
        return NULL;
    }

    /* Create SSL context */
    client->ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (!client->ssl_ctx) {
        free(client);
        return NULL;
    }

    /* Enable SSL session caching for faster reconnects */
    SSL_CTX_set_session_cache_mode(client->ssl_ctx, SSL_SESS_CACHE_CLIENT);
    SSL_CTX_sess_set_cache_size(client->ssl_ctx, 1024);  /* Cache up to 1024 sessions */

    /* Default configuration */
    client->timeout_ms = 30000;  /* 30 seconds */
    client->follow_redirects = false;  /* Python layer handles redirects for better control */
    client->max_redirects = 10;
    client->io_engine = default_io_engine;

    /* Default to Chrome browser profile */
    client->browser_profile = &PROFILE_CHROME_131;
    configure_ssl_ctx(client->ssl_ctx, client->browser_profile);

    return client;
}

/**
 * Get the connection pool from a client
 */
httpmorph_pool_t* httpmorph_client_get_pool(httpmorph_client_t *client) {
    if (!client) {
        return NULL;
    }
    return client->pool;
}

/**
 * Destroy an HTTP client
 */
void httpmorph_client_destroy(httpmorph_client_t *client) {
    if (!client) {
        return;
    }

    if (client->ssl_ctx) {
        SSL_CTX_free(client->ssl_ctx);
    }

    free(client);
}

/**
 * Create a new session
 */
httpmorph_session_t* httpmorph_session_create(httpmorph_browser_t browser_type) {
    httpmorph_session_t *session = calloc(1, sizeof(httpmorph_session_t));
    if (!session) {
        return NULL;
    }

    /* Create internal client */
    session->client = httpmorph_client_create();
    if (!session->client) {
        free(session);
        return NULL;
    }

    /* Select browser profile */
    const char *browser_str = "chrome";
    switch (browser_type) {
        case HTTPMORPH_BROWSER_CHROME: browser_str = "chrome"; break;
        case HTTPMORPH_BROWSER_FIREFOX: browser_str = "firefox"; break;
        case HTTPMORPH_BROWSER_SAFARI: browser_str = "safari"; break;
        case HTTPMORPH_BROWSER_EDGE: browser_str = "edge"; break;
        case HTTPMORPH_BROWSER_RANDOM:
            session->browser_profile = browser_profile_random();
            break;
        default: browser_str = "chrome"; break;
    }

    if (browser_type != HTTPMORPH_BROWSER_RANDOM) {
        session->browser_profile = browser_profile_by_type(browser_str);
    }

    if (session->browser_profile) {
        session->client->browser_profile = session->browser_profile;
        configure_ssl_ctx(session->client->ssl_ctx, session->browser_profile);
    }

    /* Initialize cookie jar */
    session->cookies = NULL;
    session->cookie_count = 0;

    /* Initialize connection pool for keep-alive */
    session->pool = pool_create();
    if (!session->pool) {
        httpmorph_session_destroy(session);
        return NULL;
    }

    return session;
}

/* Cookie management functions */

/**
 * Free a cookie
 */
static void cookie_free(cookie_t *cookie) {
    if (!cookie) return;
    free(cookie->name);
    free(cookie->value);
    free(cookie->domain);
    free(cookie->path);
    free(cookie);
}

/**
 * Parse Set-Cookie header and add to session
 */
static void parse_set_cookie(httpmorph_session_t *session, const char *header_value, const char *request_domain) {
    if (!session || !header_value || !request_domain) {
        return;
    }

    cookie_t *cookie = calloc(1, sizeof(cookie_t));
    if (!cookie) return;

    /* Parse cookie: name=value; attributes */
    char *header_copy = strdup(header_value);
    if (!header_copy) {
        free(cookie);
        return;
    }

    /* Extract name=value */
    char *semicolon = strchr(header_copy, ';');
    if (semicolon) *semicolon = '\0';

    char *equals = strchr(header_copy, '=');
    if (equals) {
        *equals = '\0';
        cookie->name = strdup(header_copy);
        cookie->value = strdup(equals + 1);
    } else {
        free(header_copy);
        free(cookie);
        return;
    }

    /* Set default domain and path */
    cookie->domain = strdup(request_domain);
    cookie->path = strdup("/");
    cookie->expires = 0;  /* Session cookie by default */
    cookie->secure = false;
    cookie->http_only = false;

    /* Parse attributes if present */
    if (semicolon) {
        char *attr = semicolon + 1;
        while (attr && *attr) {
            /* Skip whitespace */
            while (*attr == ' ') attr++;

            if (strncasecmp(attr, "Domain=", 7) == 0) {
                attr += 7;
                char *end = strchr(attr, ';');
                size_t len = end ? (size_t)(end - attr) : strlen(attr);
                free(cookie->domain);
                cookie->domain = strndup(attr, len);
                attr = end;
            } else if (strncasecmp(attr, "Path=", 5) == 0) {
                attr += 5;
                char *end = strchr(attr, ';');
                size_t len = end ? (size_t)(end - attr) : strlen(attr);
                free(cookie->path);
                cookie->path = strndup(attr, len);
                attr = end;
            } else if (strncasecmp(attr, "Secure", 6) == 0) {
                cookie->secure = true;
                attr = strchr(attr, ';');
            } else if (strncasecmp(attr, "HttpOnly", 8) == 0) {
                cookie->http_only = true;
                attr = strchr(attr, ';');
            } else {
                /* Skip unknown attribute */
                attr = strchr(attr, ';');
            }

            if (attr) attr++;
        }
    }

    free(header_copy);

    /* Add to session's cookie list */
    cookie->next = session->cookies;
    session->cookies = cookie;
    session->cookie_count++;
}

/**
 * Get cookies for a request
 */
static char* get_cookies_for_request(httpmorph_session_t *session, const char *domain, const char *path, bool is_secure) {
    if (!session || !domain || !path) return NULL;
    if (session->cookie_count == 0) return NULL;

    /* Build cookie header value */
    char *cookie_header = malloc(4096);
    if (!cookie_header) return NULL;

    cookie_header[0] = '\0';
    bool first = true;

    cookie_t *cookie = session->cookies;
    while (cookie) {
        /* Check if cookie matches request */
        bool domain_match = (strcasecmp(cookie->domain, domain) == 0 ||
                            (cookie->domain[0] == '.' && strstr(domain, cookie->domain + 1) != NULL));
        bool path_match = (strncmp(cookie->path, path, strlen(cookie->path)) == 0);
        bool secure_match = (!cookie->secure || is_secure);

        if (domain_match && path_match && secure_match) {
            if (!first) {
                strcat(cookie_header, "; ");
            }
            strcat(cookie_header, cookie->name);
            strcat(cookie_header, "=");
            strcat(cookie_header, cookie->value);
            first = false;
        }

        cookie = cookie->next;
    }

    if (cookie_header[0] == '\0') {
        free(cookie_header);
        return NULL;
    }

    return cookie_header;
}

/**
 * Destroy a session
 */
void httpmorph_session_destroy(httpmorph_session_t *session) {
    if (!session) {
        return;
    }

    /* Destroy connection pool (closes all pooled connections) */
    if (session->pool) {
        pool_destroy(session->pool);
    }

    if (session->client) {
        httpmorph_client_destroy(session->client);
    }

    /* Free cookies */
    cookie_t *cookie = session->cookies;
    while (cookie) {
        cookie_t *next = cookie->next;
        cookie_free(cookie);
        cookie = next;
    }

    free(session);
}

/* Response management */

/**
 * Create a new response object
 */
static httpmorph_response_t* response_create(void) {
    httpmorph_response_t *resp = calloc(1, sizeof(httpmorph_response_t));
    if (!resp) {
        return NULL;
    }

    resp->body_capacity = 16384;  /* 16KB initial */
    resp->body = malloc(resp->body_capacity);
    if (!resp->body) {
        free(resp);
        return NULL;
    }

    return resp;
}

/**
 * Destroy a response
 */
void httpmorph_response_destroy(httpmorph_response_t *response) {
    if (!response) {
        return;
    }

    /* Free headers */
    for (size_t i = 0; i < response->header_count; i++) {
        free(response->header_keys[i]);
        free(response->header_values[i]);
    }
    free(response->header_keys);
    free(response->header_values);

    /* Free body */
    free(response->body);

    /* Free TLS info */
    free(response->tls_version);
    free(response->tls_cipher);
    free(response->ja3_fingerprint);

    /* Free error message */
    free(response->error_message);

    free(response);
}

/* Helper: TCP connect with timeout */
static int tcp_connect(const char *host, uint16_t port, uint32_t timeout_ms,
                       uint64_t *connect_time_us) {
    struct addrinfo hints, *result, *rp;
    int sockfd = -1;
    uint64_t start_time = get_time_us();

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    /* Convert port to string */
    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%u", port);

    /* Resolve hostname */
    int ret = getaddrinfo(host, port_str, &hints, &result);
    if (ret != 0) {
        return -1;
    }

    /* Try each address until we succeed */
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1) {
            continue;
        }

        /* Set socket to non-blocking for timeout support */
#ifdef _WIN32
        u_long mode = 1;
        ioctlsocket(sockfd, FIONBIO, &mode);
#else
        int flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
#endif

        /* Attempt connection */
        ret = connect(sockfd, rp->ai_addr, rp->ai_addrlen);
        if (ret == 0) {
            /* Connected immediately */
            break;
        }

#ifdef _WIN32
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
#else
        if (errno == EINPROGRESS) {
#endif
            /* Connection in progress - wait with select */
            fd_set write_fds;
            struct timeval tv;

            FD_ZERO(&write_fds);
            FD_SET(sockfd, &write_fds);

            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;

            ret = select(SELECT_NFDS(sockfd), NULL, &write_fds, NULL, &tv);
            if (ret > 0) {
                /* Check if connection succeeded */
                int error = 0;
                socklen_t len = sizeof(error);
#ifdef _WIN32
                if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, (char*)&error, (int*)&len) == 0 && error == 0) {
#else
                if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) == 0 && error == 0) {
#endif
                    break;  /* Success */
                }
            }
        }

        /* Connection failed, try next address */
        close(sockfd);
        sockfd = -1;
    }

    freeaddrinfo(result);

    if (sockfd != -1) {
        /* Set socket back to blocking mode */
#ifdef _WIN32
        u_long mode = 0;
        ioctlsocket(sockfd, FIONBIO, &mode);
#else
        int flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags & ~O_NONBLOCK);
#endif

        /* Set performance options */
        int opt = 1;
#ifdef _WIN32
        setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char*)&opt, sizeof(opt));
#else
        setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
#endif

        /* Set receive timeout to prevent indefinite blocking */
#ifdef _WIN32
        DWORD timeout_dw = timeout_ms;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout_dw, sizeof(timeout_dw));
#else
        struct timeval recv_timeout;
        recv_timeout.tv_sec = timeout_ms / 1000;
        recv_timeout.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));
#endif

        *connect_time_us = get_time_us() - start_time;
    }

    return sockfd;
}

/* Helper: Parse proxy URL */
static int parse_proxy_url(const char *proxy_url, char **host, uint16_t *port,
                           char **username, char **password) {
    if (!proxy_url || !host || !port) {
        return -1;
    }

    *host = NULL;
    *port = 0;
    if (username) *username = NULL;
    if (password) *password = NULL;

    /* Parse proxy URL format: [http://][username:password@]host:port */
    const char *start = proxy_url;

    /* Skip http:// or https:// */
    if (strncmp(start, "http://", 7) == 0) {
        start += 7;
    } else if (strncmp(start, "https://", 8) == 0) {
        start += 8;
    }

    /* Check for username:password@ */
    const char *at_sign = strchr(start, '@');
    if (at_sign && username && password) {
        const char *colon = strchr(start, ':');
        if (colon && colon < at_sign) {
            size_t user_len = colon - start;
            size_t pass_len = at_sign - colon - 1;

            *username = strndup(start, user_len);
            *password = strndup(colon + 1, pass_len);

            start = at_sign + 1;
        }
    }

    /* Parse host:port */
    const char *colon = strchr(start, ':');
    if (colon) {
        size_t host_len = colon - start;
        *host = strndup(start, host_len);
        *port = (uint16_t)atoi(colon + 1);
    } else {
        *host = strdup(start);
        *port = 8080;  /* Default proxy port */
    }

    return (*host != NULL) ? 0 : -1;
}

/* Helper: Base64 encode for proxy authentication */
static char* base64_encode(const char *input, size_t length) {
    static const char base64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    size_t output_length = 4 * ((length + 2) / 3);
    char *output = malloc(output_length + 1);
    if (!output) return NULL;

    size_t i, j;
    for (i = 0, j = 0; i < length;) {
        uint32_t octet_a = i < length ? (unsigned char)input[i++] : 0;
        uint32_t octet_b = i < length ? (unsigned char)input[i++] : 0;
        uint32_t octet_c = i < length ? (unsigned char)input[i++] : 0;

        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

        output[j++] = base64_chars[(triple >> 3 * 6) & 0x3F];
        output[j++] = base64_chars[(triple >> 2 * 6) & 0x3F];
        output[j++] = base64_chars[(triple >> 1 * 6) & 0x3F];
        output[j++] = base64_chars[(triple >> 0 * 6) & 0x3F];
    }

    /* Handle padding */
    int padding = (3 - (length % 3)) % 3;
    for (int k = 0; k < padding; k++) {
        output[output_length - 1 - k] = '=';
    }

    output[output_length] = '\0';
    return output;
}

/* Helper: Send HTTP CONNECT request to proxy */
static int proxy_connect(int sockfd, const char *target_host, uint16_t target_port,
                        const char *username, const char *password, uint32_t timeout_ms) {
    char connect_req[2048];
    int len;

    /* Build CONNECT request */
    len = snprintf(connect_req, sizeof(connect_req),
                   "CONNECT %s:%u HTTP/1.1\r\n"
                   "Host: %s:%u\r\n",
                   target_host, target_port, target_host, target_port);

    /* Add Proxy-Authorization if credentials provided */
    if (username && password) {
        char credentials[512];
        snprintf(credentials, sizeof(credentials), "%s:%s", username, password);

        char *encoded = base64_encode(credentials, strlen(credentials));
        if (encoded) {
            len += snprintf(connect_req + len, sizeof(connect_req) - len,
                          "Proxy-Authorization: Basic %s\r\n", encoded);
            free(encoded);
        }
    }

    /* End headers */
    len += snprintf(connect_req + len, sizeof(connect_req) - len, "\r\n");

    /* Send CONNECT request */
    ssize_t sent = send(sockfd, connect_req, len, 0);
    if (sent != len) {
        return -1;
    }

    /* Read response */
    char response[4096];
    ssize_t received = recv(sockfd, response, sizeof(response) - 1, 0);
    if (received <= 0) {
        return -1;
    }
    response[received] = '\0';

    /* Check for 200 Connection established */
    if (strncmp(response, "HTTP/1", 6) == 0) {
        char *space = strchr(response, ' ');
        if (space) {
            int status = atoi(space + 1);
            if (status == 200) {
                return 0;  /* Success */
            }
        }
    }

    return -1;  /* Proxy connection failed */
}

/* Helper: TLS connect with browser fingerprint */
static SSL* tls_connect(SSL_CTX *ctx, int sockfd, const char *hostname,
                        const browser_profile_t *profile, bool http2_enabled,
                        uint64_t *tls_time_us) {
    uint64_t start_time = get_time_us();

    SSL *ssl = SSL_new(ctx);
    if (!ssl) {
        return NULL;
    }

    /* Set ALPN protocols based on http2_enabled flag */
    if (profile && profile->alpn_protocol_count > 0) {
        unsigned char alpn_list[256];
        unsigned char *alpn_p = alpn_list;

        for (int i = 0; i < profile->alpn_protocol_count; i++) {
            /* Skip "h2" if HTTP/2 not enabled */
            if (!http2_enabled && strcmp(profile->alpn_protocols[i], "h2") == 0) {
                continue;
            }

            size_t len = strlen(profile->alpn_protocols[i]);
            *alpn_p++ = (unsigned char)len;
            memcpy(alpn_p, profile->alpn_protocols[i], len);
            alpn_p += len;
        }

        /* Only set ALPN if we have protocols */
        if (alpn_p > alpn_list) {
            SSL_set_alpn_protos(ssl, alpn_list, alpn_p - alpn_list);
        }
    }

    /* Set SNI hostname */
    SSL_set_tlsext_host_name(ssl, hostname);

    /* Attach to socket */
    if (SSL_set_fd(ssl, sockfd) != 1) {
        SSL_free(ssl);
        return NULL;
    }

    /* Perform TLS handshake */
    int ret = SSL_connect(ssl);
    if (ret != 1) {
        SSL_free(ssl);
        return NULL;
    }

    *tls_time_us = get_time_us() - start_time;
    return ssl;
}

/* Helper: Calculate JA3 fingerprint from SSL connection and browser profile */
static char* calculate_ja3_fingerprint(SSL *ssl, const browser_profile_t *profile) {
    if (!ssl) {
        return NULL;
    }

    char ja3_string[4096];
    char *p = ja3_string;
    char *end = ja3_string + sizeof(ja3_string);

    /* Ensure we don't overflow */
    if (end <= p) {
        return NULL;
    }

    /* JA3 Format: TLSVersion,Ciphers,Extensions,EllipticCurves,EllipticCurvePointFormats */

    /* 1. TLS Version */
    int tls_version = SSL_version(ssl);
    uint16_t ja3_version = 0;
    switch (tls_version) {
        case TLS1_VERSION:   ja3_version = 0x0301; break;  /* TLS 1.0 */
        case TLS1_1_VERSION: ja3_version = 0x0302; break;  /* TLS 1.1 */
        case TLS1_2_VERSION: ja3_version = 0x0303; break;  /* TLS 1.2 */
        case TLS1_3_VERSION: ja3_version = 0x0304; break;  /* TLS 1.3 */
        default:             ja3_version = 0x0303; break;  /* Default to TLS 1.2 */
    }

    int written = snprintf(p, SNPRINTF_SIZE(end - p), "%u", ja3_version);
    if (written < 0 || written >= (end - p)) {
        return NULL;
    }
    p += written;

    /* 2. Cipher Suites - use browser profile's cipher list to make it unique */
    if (p < end) *p++ = ',';
    if (profile && profile->cipher_suite_count > 0) {
        for (size_t i = 0; i < profile->cipher_suite_count && p < end; i++) {
            if (i > 0 && p < end) *p++ = '-';
            written = snprintf(p, SNPRINTF_SIZE(end - p), "%u", profile->cipher_suites[i]);
            if (written > 0 && written < (end - p)) p += written;
        }
    } else {
        /* Fallback: use negotiated cipher */
        const SSL_CIPHER *cipher = SSL_get_current_cipher(ssl);
        if (cipher) {
            uint16_t cipher_id = SSL_CIPHER_get_id(cipher) & 0xFFFF;
            written = snprintf(p, SNPRINTF_SIZE(end - p), "%u", cipher_id);
            if (written > 0 && written < (end - p)) p += written;
        }
    }

    /* 3. Extensions - use browser profile's extension list */
    if (p < end) *p++ = ',';
    if (profile && profile->extension_count > 0) {
        for (size_t i = 0; i < profile->extension_count && p < end; i++) {
            if (i > 0 && p < end) *p++ = '-';
            written = snprintf(p, SNPRINTF_SIZE(end - p), "%u", profile->extensions[i]);
            if (written > 0 && written < (end - p)) p += written;
        }
    } else {
        /* Fallback: common extensions */
        written = snprintf(p, SNPRINTF_SIZE(end - p), "0-10-11-13-16-23-35-43-45-51");
        if (written > 0 && written < (end - p)) p += written;
    }

    /* 4. Elliptic Curves - use browser profile's curve list */
    if (p < end) *p++ = ',';
    if (profile && profile->curve_count > 0) {
        for (size_t i = 0; i < profile->curve_count && p < end; i++) {
            if (i > 0 && p < end) *p++ = '-';
            written = snprintf(p, SNPRINTF_SIZE(end - p), "%u", profile->curves[i]);
            if (written > 0 && written < (end - p)) p += written;
        }
    } else {
        /* Fallback: common curves */
        written = snprintf(p, SNPRINTF_SIZE(end - p), "29-23-24");
        if (written > 0 && written < (end - p)) p += written;
    }

    /* 5. Elliptic Curve Point Formats */
    if (p < end - 2) {
        *p++ = ',';
        *p++ = '0';
        *p = '\0';
    }

    /* Calculate MD5 hash of the JA3 string using EVP interface */
    unsigned char md5_digest[16];  /* MD5 produces 16 bytes */
    unsigned int md5_len = 0;

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        return NULL;
    }

    if (EVP_DigestInit_ex(mdctx, EVP_md5(), NULL) != 1 ||
        EVP_DigestUpdate(mdctx, ja3_string, strlen(ja3_string)) != 1 ||
        EVP_DigestFinal_ex(mdctx, md5_digest, &md5_len) != 1) {
        EVP_MD_CTX_free(mdctx);
        return NULL;
    }
    EVP_MD_CTX_free(mdctx);

    /* Convert MD5 to hex string */
    char *ja3_hash = malloc(33);  /* 32 hex chars + null terminator */
    if (!ja3_hash) {
        return NULL;
    }

    for (int i = 0; i < 16; i++) {
        snprintf(ja3_hash + (i * 2), 3, "%02x", md5_digest[i]);
    }
    ja3_hash[32] = '\0';

    return ja3_hash;
}

/* Helper: Send HTTP request */
static int send_http_request(SSL *ssl, int sockfd, const httpmorph_request_t *request,
                             const char *host, const char *path, const char *scheme,
                             uint16_t port, bool use_proxy, const char *proxy_user,
                             const char *proxy_pass) {
    char request_buf[8192];
    char *p = request_buf;
    char *end = request_buf + sizeof(request_buf);

    /* Request line */
    const char *method_str = "GET";
    switch (request->method) {
        case HTTPMORPH_GET:     method_str = "GET"; break;
        case HTTPMORPH_POST:    method_str = "POST"; break;
        case HTTPMORPH_PUT:     method_str = "PUT"; break;
        case HTTPMORPH_DELETE:  method_str = "DELETE"; break;
        case HTTPMORPH_HEAD:    method_str = "HEAD"; break;
        case HTTPMORPH_OPTIONS: method_str = "OPTIONS"; break;
        case HTTPMORPH_PATCH:   method_str = "PATCH"; break;
        default: break;
    }

    /* For HTTP proxy (not HTTPS/CONNECT), use full URL in request line */
    if (use_proxy && !ssl) {
        /* HTTP proxy requires absolute URI: GET http://host:port/path HTTP/1.1 */
        if ((strcmp(scheme, "http") == 0 && port == 80) ||
            (strcmp(scheme, "https") == 0 && port == 443)) {
            p += snprintf(p, SNPRINTF_SIZE(end - p), "%s %s://%s%s HTTP/1.1\r\n",
                         method_str, scheme, host, path);
        } else {
            p += snprintf(p, SNPRINTF_SIZE(end - p), "%s %s://%s:%u%s HTTP/1.1\r\n",
                         method_str, scheme, host, port, path);
        }
    } else {
        /* Direct connection or HTTPS through proxy (after CONNECT): use relative path */
        p += snprintf(p, SNPRINTF_SIZE(end - p), "%s %s HTTP/1.1\r\n", method_str, path);
    }

    /* Add Host header with port if non-standard */
    if ((strcmp(scheme, "http") == 0 && port != 80) || (strcmp(scheme, "https") == 0 && port != 443)) {
        p += snprintf(p, SNPRINTF_SIZE(end - p), "Host: %s:%u\r\n", host, port);
    } else {
        p += snprintf(p, SNPRINTF_SIZE(end - p), "Host: %s\r\n", host);
    }

    /* Add minimal required headers if not provided */
    bool has_user_agent = false;
    bool has_accept = false;
    bool has_connection = false;

    for (size_t i = 0; i < request->header_count; i++) {
        if (strcasecmp(request->header_keys[i], "User-Agent") == 0) has_user_agent = true;
        if (strcasecmp(request->header_keys[i], "Accept") == 0) has_accept = true;
        if (strcasecmp(request->header_keys[i], "Connection") == 0) has_connection = true;
    }

    /* Add default headers if missing */
    if (!has_user_agent) {
        /* Use request's user agent if set, otherwise use generic */
        const char *user_agent = request->user_agent ? request->user_agent : "httpmorph/0.1.3";
        p += snprintf(p, SNPRINTF_SIZE(end - p), "User-Agent: %s\r\n", user_agent);
    }
    if (!has_accept) {
        p += snprintf(p, SNPRINTF_SIZE(end - p), "Accept: */*\r\n");
    }
    if (!has_connection) {
        /* Use keep-alive for connection reuse */
        p += snprintf(p, SNPRINTF_SIZE(end - p), "Connection: keep-alive\r\n");
    }

    /* Add Proxy-Authorization header for HTTP proxy (not HTTPS/CONNECT) */
    if (use_proxy && !ssl && (proxy_user || proxy_pass)) {
        const char *username = proxy_user ? proxy_user : "";
        const char *password = proxy_pass ? proxy_pass : "";

        char credentials[512];
        snprintf(credentials, sizeof(credentials), "%s:%s", username, password);

        char *encoded = base64_encode(credentials, strlen(credentials));
        if (encoded) {
            p += snprintf(p, SNPRINTF_SIZE(end - p), "Proxy-Authorization: Basic %s\r\n", encoded);
            free(encoded);
        }
    }

    /* Add request headers */
    for (size_t i = 0; i < request->header_count; i++) {
        p += snprintf(p, SNPRINTF_SIZE(end - p), "%s: %s\r\n",
                     request->header_keys[i], request->header_values[i]);
    }

    /* Content-Length if body present */
    if (request->body && request->body_len > 0) {
        p += snprintf(p, SNPRINTF_SIZE(end - p), "Content-Length: %zu\r\n", request->body_len);
    }

    /* End of headers */
    p += snprintf(p, SNPRINTF_SIZE(end - p), "\r\n");

    /* Send request headers */
    size_t total_sent = 0;
    size_t header_len = p - request_buf;


    if (ssl) {
        while (total_sent < header_len) {
            int sent = SSL_write(ssl, request_buf + total_sent, header_len - total_sent);
            if (sent <= 0) {
                return -1;
            }
            total_sent += sent;
        }

        /* Send body if present */
        if (request->body && request->body_len > 0) {
            total_sent = 0;
            while (total_sent < request->body_len) {
                int sent = SSL_write(ssl, request->body + total_sent,
                                   request->body_len - total_sent);
                if (sent <= 0) return -1;
                total_sent += sent;
            }
        }
    } else {
        while (total_sent < header_len) {
            ssize_t sent = send(sockfd, request_buf + total_sent,
                               header_len - total_sent, 0);
            if (sent <= 0) return -1;
            total_sent += sent;
        }

        /* Send body if present */
        if (request->body && request->body_len > 0) {
            total_sent = 0;
            while (total_sent < request->body_len) {
                ssize_t sent = send(sockfd, request->body + total_sent,
                                   request->body_len - total_sent, 0);
                if (sent <= 0) return -1;
                total_sent += sent;
            }
        }
    }

    return 0;
}

/* Helper: Parse HTTP response line */
static int parse_response_line(const char *line, httpmorph_response_t *response) {
    /* Parse: HTTP/1.1 200 OK */
    int major, minor, status;
    if (sscanf(line, "HTTP/%d.%d %d", &major, &minor, &status) == 3) {
        response->status_code = status;
        if (major == 1 && minor == 1) {
            response->http_version = HTTPMORPH_VERSION_1_1;
        } else if (major == 1 && minor == 0) {
            response->http_version = HTTPMORPH_VERSION_1_0;
        } else if (major == 2) {
            response->http_version = HTTPMORPH_VERSION_2_0;
        }
        return 0;
    }
    return -1;
}

/* Helper: Add response header */
static int add_response_header(httpmorph_response_t *response,
                               const char *key, const char *value) {
    size_t new_count = response->header_count + 1;

    char **new_keys = realloc(response->header_keys, new_count * sizeof(char*));
    char **new_values = realloc(response->header_values, new_count * sizeof(char*));

    if (!new_keys || !new_values) {
        return -1;
    }

    new_keys[response->header_count] = strdup(key);
    new_values[response->header_count] = strdup(value);

    response->header_keys = new_keys;
    response->header_values = new_values;
    response->header_count = new_count;

    return 0;
}

/* Helper: Convert method enum to string */
static const char* httpmorph_method_to_string(httpmorph_method_t method) {
    switch (method) {
        case HTTPMORPH_GET: return "GET";
        case HTTPMORPH_POST: return "POST";
        case HTTPMORPH_PUT: return "PUT";
        case HTTPMORPH_DELETE: return "DELETE";
        case HTTPMORPH_HEAD: return "HEAD";
        case HTTPMORPH_OPTIONS: return "OPTIONS";
        case HTTPMORPH_PATCH: return "PATCH";
        default: return "GET";
    }
}

/* Helper: Add header to response (with length for HTTP/2) */
static int httpmorph_response_add_header(httpmorph_response_t *response,
                                          const char *name, size_t namelen,
                                          const char *value, size_t valuelen) {
    /* Skip pseudo-headers for HTTP/2 */
    if (namelen > 0 && name[0] == ':') {
        return 0;
    }

    /* Allocate new arrays */
    size_t new_count = response->header_count + 1;
    char **new_keys = realloc(response->header_keys, new_count * sizeof(char *));
    char **new_values = realloc(response->header_values, new_count * sizeof(char *));

    if (!new_keys || !new_values) {
        free(new_keys);
        free(new_values);
        return -1;
    }

    /* Copy header */
    new_keys[response->header_count] = strndup(name, namelen);
    new_values[response->header_count] = strndup(value, valuelen);

    if (!new_keys[response->header_count] || !new_values[response->header_count]) {
        free(new_keys[response->header_count]);
        free(new_values[response->header_count]);
        free(new_keys);
        free(new_values);
        return -1;
    }

    response->header_keys = new_keys;
    response->header_values = new_values;
    response->header_count = new_count;

    return 0;
}

#ifdef HAVE_NGHTTP2
/* HTTP/2 Support Functions */

/* Data structure for collecting HTTP/2 response */
typedef struct {
    httpmorph_response_t *response;
    uint8_t *data_buf;        /* Response body buffer */
    size_t data_capacity;
    size_t data_len;
    bool headers_complete;
    bool stream_closed;
    SSL *ssl;                 /* SSL connection for send/recv */

    /* Request body fields */
    const uint8_t *req_body;  /* Request body to send */
    size_t req_body_len;      /* Total length of request body */
    size_t req_body_sent;     /* Bytes already sent */
} http2_stream_data_t;

/* Helper: Send data over SSL or socket */
static ssize_t http2_send_callback(nghttp2_session *session, const uint8_t *data,
                                    size_t length, int flags, void *user_data) {
    http2_stream_data_t *stream_data = (http2_stream_data_t *)user_data;
    if (stream_data && stream_data->ssl) {
        return SSL_write(stream_data->ssl, data, length);
    }
    return -1;
}

/* Helper: Data provider callback for sending request body */
static ssize_t http2_data_source_read_callback(nghttp2_session *session, int32_t stream_id,
                                                 uint8_t *buf, size_t length, uint32_t *data_flags,
                                                 nghttp2_data_source *source, void *user_data) {
    http2_stream_data_t *stream_data = (http2_stream_data_t *)user_data;

    size_t remaining = stream_data->req_body_len - stream_data->req_body_sent;
    size_t to_send = remaining < length ? remaining : length;

    if (to_send > 0) {
        memcpy(buf, stream_data->req_body + stream_data->req_body_sent, to_send);
        stream_data->req_body_sent += to_send;
    }

    /* Set EOF flag if all data sent */
    if (stream_data->req_body_sent >= stream_data->req_body_len) {
        *data_flags |= NGHTTP2_DATA_FLAG_EOF;
    }

    return to_send;
}

/* Helper: Receive data from SSL or socket */
static ssize_t http2_recv_callback(nghttp2_session *session, uint8_t *buf,
                                    size_t length, int flags, void *user_data) {
    http2_stream_data_t *stream_data = (http2_stream_data_t *)user_data;
    if (stream_data && stream_data->ssl) {
        int n = SSL_read(stream_data->ssl, buf, length);
        if (n < 0) {
            int ssl_err = SSL_get_error(stream_data->ssl, n);
            if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
                return NGHTTP2_ERR_WOULDBLOCK;
            }
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        } else if (n == 0) {
            /* EOF - remote peer closed connection */
            return NGHTTP2_ERR_EOF;
        }
        return n;
    }
    return NGHTTP2_ERR_CALLBACK_FAILURE;
}

/* Helper: Called when a header name/value pair is received */
static int http2_on_header_callback(nghttp2_session *session,
                                     const nghttp2_frame *frame,
                                     const uint8_t *name, size_t namelen,
                                     const uint8_t *value, size_t valuelen,
                                     uint8_t flags, void *user_data) {
    http2_stream_data_t *stream_data = (http2_stream_data_t *)user_data;

    if (frame->hd.type != NGHTTP2_HEADERS || frame->headers.cat != NGHTTP2_HCAT_RESPONSE) {
        return 0;
    }

    /* Handle :status pseudo-header */
    if (namelen == 7 && memcmp(name, ":status", 7) == 0) {
        char status_str[4] = {0};
        size_t copy_len = valuelen > 3 ? 3 : valuelen;
        memcpy(status_str, value, copy_len);
        stream_data->response->status_code = atoi(status_str);
        return 0;
    }

    /* Add regular header */
    httpmorph_response_add_header(stream_data->response, (const char *)name,
                                   namelen, (const char *)value, valuelen);
    return 0;
}

/* Helper: Called when DATA frame is received */
static int http2_on_data_chunk_recv_callback(nghttp2_session *session, uint8_t flags,
                                               int32_t stream_id, const uint8_t *data,
                                               size_t len, void *user_data) {
    http2_stream_data_t *stream_data = (http2_stream_data_t *)user_data;

    /* Expand buffer if needed */
    if (stream_data->data_len + len > stream_data->data_capacity) {
        size_t new_capacity = (stream_data->data_len + len) * 2;
        uint8_t *new_buf = realloc(stream_data->data_buf, new_capacity);
        if (!new_buf) return NGHTTP2_ERR_CALLBACK_FAILURE;
        stream_data->data_buf = new_buf;
        stream_data->data_capacity = new_capacity;
    }

    memcpy(stream_data->data_buf + stream_data->data_len, data, len);
    stream_data->data_len += len;
    return 0;
}

/* Helper: Called when a frame is received */
static int http2_on_frame_recv_callback(nghttp2_session *session,
                                         const nghttp2_frame *frame, void *user_data) {
    http2_stream_data_t *stream_data = (http2_stream_data_t *)user_data;

    if (frame->hd.type == NGHTTP2_HEADERS &&
        frame->headers.cat == NGHTTP2_HCAT_RESPONSE) {
        stream_data->headers_complete = true;
    }

    /* Check if stream is closed - only check END_STREAM on HEADERS or DATA frames */
    if ((frame->hd.type == NGHTTP2_HEADERS || frame->hd.type == NGHTTP2_DATA) &&
        (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) &&
        frame->hd.stream_id > 0) {  /* Only for non-zero stream IDs */
        stream_data->stream_closed = true;
    }
    return 0;
}

/* Perform HTTP/2 request */
static int http2_request(SSL *ssl, const httpmorph_request_t *request,
                         const char *host, const char *path,
                         httpmorph_response_t *response) {
    nghttp2_session *session;
    nghttp2_session_callbacks *callbacks;
    http2_stream_data_t stream_data = {0};
    int rv;

    stream_data.response = response;
    stream_data.ssl = ssl;
    stream_data.data_capacity = 16384;
    stream_data.data_buf = malloc(stream_data.data_capacity);
    if (!stream_data.data_buf) return -1;

    /* Set up request body if present */
    stream_data.req_body = (const uint8_t *)request->body;
    stream_data.req_body_len = request->body_len;
    stream_data.req_body_sent = 0;

    /* Initialize nghttp2 callbacks */
    nghttp2_session_callbacks_new(&callbacks);
    nghttp2_session_callbacks_set_send_callback(callbacks, http2_send_callback);
    nghttp2_session_callbacks_set_recv_callback(callbacks, http2_recv_callback);
    nghttp2_session_callbacks_set_on_header_callback(callbacks, http2_on_header_callback);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, http2_on_data_chunk_recv_callback);
    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, http2_on_frame_recv_callback);

    /* Create HTTP/2 client session (pass &stream_data as user_data) */
    rv = nghttp2_session_client_new(&session, callbacks, &stream_data);
    nghttp2_session_callbacks_del(callbacks);
    if (rv != 0) {
        free(stream_data.data_buf);
        return -1;
    }

    /* Send connection preface and initial SETTINGS */
    nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, NULL, 0);
    nghttp2_session_send(session);

    /* Prepare request headers */
    nghttp2_nv hdrs[64];
    int nhdrs = 0;

    /* Add pseudo-headers first */
    const char *method_str = httpmorph_method_to_string(request->method);
    hdrs[nhdrs++] = (nghttp2_nv){(uint8_t *)":method", (uint8_t *)method_str, 7, strlen(method_str), NGHTTP2_NV_FLAG_NONE};
    hdrs[nhdrs++] = (nghttp2_nv){(uint8_t *)":path", (uint8_t *)path, 5, strlen(path), NGHTTP2_NV_FLAG_NONE};
    hdrs[nhdrs++] = (nghttp2_nv){(uint8_t *)":scheme", (uint8_t *)"https", 7, 5, NGHTTP2_NV_FLAG_NONE};
    hdrs[nhdrs++] = (nghttp2_nv){(uint8_t *)":authority", (uint8_t *)host, 10, strlen(host), NGHTTP2_NV_FLAG_NONE};

    /* Add custom headers */
    for (size_t i = 0; i < request->header_count && nhdrs < 60; i++) {
        /* Skip headers that conflict with pseudo-headers */
        if (strcasecmp(request->header_keys[i], "host") == 0) continue;

        hdrs[nhdrs++] = (nghttp2_nv){
            (uint8_t *)request->header_keys[i],
            (uint8_t *)request->header_values[i],
            strlen(request->header_keys[i]),
            strlen(request->header_values[i]),
            NGHTTP2_NV_FLAG_NONE
        };
    }

    /* Set up data provider if request has a body */
    nghttp2_data_provider data_prd;
    nghttp2_data_provider *data_prd_ptr = NULL;

    if (stream_data.req_body_len > 0) {
        data_prd.source.ptr = NULL;
        data_prd.read_callback = http2_data_source_read_callback;
        data_prd_ptr = &data_prd;
    }

    /* Submit request with data provider if body present */
    int32_t stream_id = nghttp2_submit_request(session, NULL, hdrs, nhdrs, data_prd_ptr, NULL);
    if (stream_id < 0) {
        nghttp2_session_del(session);
        free(stream_data.data_buf);
        return -1;
    }

    /* Send request */
    nghttp2_session_send(session);

    /* Receive response - event loop for non-blocking I/O */
    int sockfd = SSL_get_fd(ssl);
    fd_set readfds, writefds;
    struct timeval tv;

    while (!stream_data.stream_closed &&
           (nghttp2_session_want_read(session) || nghttp2_session_want_write(session))) {

        /* Wait for socket to be ready */
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);

        if (nghttp2_session_want_read(session)) {
            FD_SET(sockfd, &readfds);
        }
        if (nghttp2_session_want_write(session)) {
            FD_SET(sockfd, &writefds);
        }

        tv.tv_sec = 5;  /* 5 second timeout */
        tv.tv_usec = 0;

        int select_rv = select(SELECT_NFDS(sockfd), &readfds, &writefds, NULL, &tv);
        if (select_rv < 0) {
            /* Error */
            rv = -1;
            break;
        } else if (select_rv == 0) {
            /* Timeout */
            rv = -1;
            break;
        }

        /* Receive data if available */
        if (FD_ISSET(sockfd, &readfds)) {
            rv = nghttp2_session_recv(session);
            if (rv != 0) {
                if (rv == NGHTTP2_ERR_EOF) {
                    /* Normal termination */
                    rv = 0;
                    break;
                }
                /* Other error */
                break;
            }
        }

        /* Send data if possible */
        if (FD_ISSET(sockfd, &writefds)) {
            rv = nghttp2_session_send(session);
            if (rv != 0) {
                break;
            }
        }
    }

    /* Set rv to 0 for success if stream closed normally */
    if (stream_data.stream_closed && rv == 0) {
        rv = 0;
    }

    /* Check for errors */
    if (rv != 0) {
        nghttp2_session_del(session);
        free(stream_data.data_buf);
        return -1;
    }

    /* Copy data to response */
    if (stream_data.data_len > 0) {
        response->body = stream_data.data_buf;
        response->body_len = stream_data.data_len;
        response->body_capacity = stream_data.data_capacity;
    } else {
        free(stream_data.data_buf);
        response->body = NULL;
        response->body_len = 0;
    }

    nghttp2_session_del(session);
    return 0;
}
#endif /* HAVE_NGHTTP2 */

/* Helper: Receive HTTP response */
static int recv_http_response(SSL *ssl, int sockfd, httpmorph_response_t *response,
                              uint64_t *first_byte_time_us, bool *conn_will_close, httpmorph_method_t method) {
    char buffer[16384];
    size_t buffer_pos = 0;
    bool headers_complete = false;
    size_t content_length = 0;
    bool is_head_request = (method == HTTPMORPH_HEAD);
    bool chunked = false;
    uint64_t first_byte_time = 0;

    /* Read response headers - read in chunks, not byte by byte */
    while (!headers_complete && buffer_pos < sizeof(buffer) - 1) {
        int n;
        size_t to_read = sizeof(buffer) - buffer_pos - 1;
        if (to_read > 4096) to_read = 4096;

        if (ssl) {
            n = SSL_read(ssl, buffer + buffer_pos, to_read);
            if (n <= 0) {
                /* Check if we have any data and end of headers */
                if (buffer_pos > 0 && strstr(buffer, "\r\n\r\n")) {
                    headers_complete = true;
                    break;
                }
                /* Check for SSL timeout/errors */
                int ssl_err = SSL_get_error(ssl, n);
                if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
                    return HTTPMORPH_ERROR_TIMEOUT;
                }
                return HTTPMORPH_ERROR_NETWORK;
            }
        } else {
            n = recv(sockfd, buffer + buffer_pos, to_read, 0);
            if (n <= 0) {
                /* Check if we have any data and end of headers */
                if (buffer_pos > 0 && strstr(buffer, "\r\n\r\n")) {
                    headers_complete = true;
                    break;
                }
                /* Check errno for timeout */
#ifdef _WIN32
                int err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK || err == WSAETIMEDOUT) {
#else
                if (errno == EWOULDBLOCK || errno == EAGAIN || errno == ETIMEDOUT) {
#endif
                    return HTTPMORPH_ERROR_TIMEOUT;
                }
                return HTTPMORPH_ERROR_NETWORK;
            }
        }

        if (first_byte_time == 0) {
            first_byte_time = get_time_us();
        }

        buffer_pos += n;
        buffer[buffer_pos] = '\0';

        /* Check for end of headers */
        if (strstr(buffer, "\r\n\r\n")) {
            headers_complete = true;
        }
    }

    *first_byte_time_us = first_byte_time;

    /* Parse headers */
    char *headers_end = strstr(buffer, "\r\n\r\n");
    if (!headers_end) {
        return -1;
    }

    /* Save headers end position before modifying buffer */
    char *body_start = headers_end + 4;  /* Point to body data after \r\n\r\n */
    size_t body_in_buffer = buffer_pos - (body_start - buffer);

    char *line_start = buffer;
    char *line_end;
    bool first_line = true;

    /* Parse headers line by line up to headers_end */
    while (line_start < headers_end && (line_end = strstr(line_start, "\r\n")) != NULL) {
        *line_end = '\0';

        if (first_line) {
            if (parse_response_line(line_start, response) != 0) {
                return -1;
            }
            first_line = false;
        } else if (strlen(line_start) > 0) {
            /* Parse header: key: value */
            char *colon = strchr(line_start, ':');
            if (colon) {
                *colon = '\0';
                char *key = line_start;
                char *value = colon + 1;

                /* Skip leading whitespace in value */
                while (*value == ' ') value++;

                add_response_header(response, key, value);

                /* Check for Content-Length or Transfer-Encoding */
                if (strcasecmp(key, "Content-Length") == 0) {
                    content_length = strtoul(value, NULL, 10);
                } else if (strcasecmp(key, "Transfer-Encoding") == 0 &&
                          strstr(value, "chunked")) {
                    chunked = true;
                }
            }
        }

        line_start = line_end + 2;
    }

    /* Read response body */
    size_t body_received = 0;

    /* Copy any body data already in buffer */
    if (body_in_buffer > 0) {
        if (response->body_capacity < body_in_buffer) {
            /* Use 2x growth strategy instead of exact size */
            size_t new_capacity = body_in_buffer * 2;
            response->body = realloc(response->body, new_capacity);
            response->body_capacity = new_capacity;
        }
        memcpy(response->body, body_start, body_in_buffer);
        body_received = body_in_buffer;
    }

    /* For HEAD requests, never read body even if Content-Length is present */
    if (is_head_request) {
        response->body_len = 0;
        if (first_byte_time_us) {
            *first_byte_time_us = first_byte_time;
        }
        return 0;
    }

    /* Allocate body buffer and read based on Content-Length if known */
    if (content_length > 0 && content_length < 100 * 1024 * 1024) {
        /* Known content length - pre-allocate exact size */
        if (response->body_capacity < content_length) {
            response->body = realloc(response->body, content_length);
            response->body_capacity = content_length;
        }

        while (body_received < content_length) {
            int n;
            if (ssl) {
                n = SSL_read(ssl, response->body + body_received,
                           content_length - body_received);
            } else {
                n = recv(sockfd, response->body + body_received,
                        content_length - body_received, 0);
            }

            if (n <= 0) break;
            body_received += n;
        }
    } else if (chunked) {
        /* Chunked transfer encoding - simplified implementation */
        /* For now, read until connection closes */
        while (body_received < response->body_capacity - 1024) {
            int n;
            if (ssl) {
                n = SSL_read(ssl, response->body + body_received,
                           response->body_capacity - body_received - 1);
            } else {
                n = recv(sockfd, response->body + body_received,
                        response->body_capacity - body_received - 1, 0);
            }

            if (n <= 0) break;
            body_received += n;

            /* Expand buffer if needed */
            if (body_received > response->body_capacity - 2048) {
                size_t new_capacity = response->body_capacity * 2;
                uint8_t *new_body = realloc(response->body, new_capacity);
                if (!new_body) break;
                response->body = new_body;
                response->body_capacity = new_capacity;
            }
        }
    } else {
        /* No content length - read until EOF */
        if (conn_will_close) *conn_will_close = true;
        while (body_received < response->body_capacity - 1024) {
            int n;
            if (ssl) {
                n = SSL_read(ssl, response->body + body_received,
                           response->body_capacity - body_received - 1);
            } else {
                n = recv(sockfd, response->body + body_received,
                        response->body_capacity - body_received - 1, 0);
            }

            if (n <= 0) break;
            body_received += n;

            /* Expand buffer if needed */
            if (body_received > response->body_capacity - 2048) {
                size_t new_capacity = response->body_capacity * 2;
                uint8_t *new_body = realloc(response->body, new_capacity);
                if (!new_body) break;
                response->body = new_body;
                response->body_capacity = new_capacity;
            }
        }
    }

    response->body_len = body_received;
    return 0;
}

/* Helper: Decompress gzip-encoded body */
static int decompress_gzip(httpmorph_response_t *response) {
    if (!response || !response->body || response->body_len == 0) {
        return -1;
    }

    /* Check if body is gzip-compressed (starts with 0x1f 0x8b) */
    if (response->body_len < 2 ||
        response->body[0] != 0x1f ||
        response->body[1] != 0x8b) {
        return 0;  /* Not gzipped, nothing to do */
    }

    /* Allocate decompressed buffer (assume 10x compression ratio) */
    size_t decompressed_capacity = response->body_len * 10;
    if (decompressed_capacity < 16384) decompressed_capacity = 16384;

    uint8_t *decompressed = malloc(decompressed_capacity);
    if (!decompressed) {
        return -1;
    }

    /* Initialize zlib */
    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    stream.next_in = response->body;
    stream.avail_in = (uInt)response->body_len;  /* Cast size_t to uInt for zlib */
    stream.next_out = decompressed;
    stream.avail_out = (uInt)decompressed_capacity;  /* Cast size_t to uInt for zlib */

    /* Use inflateInit2 with windowBits=15+16 for gzip */
    int ret = inflateInit2(&stream, 15 + 16);
    if (ret != Z_OK) {
        free(decompressed);
        return -1;
    }

    /* Decompress */
    ret = inflate(&stream, Z_FINISH);

    /* Handle need for more output space */
    while (ret == Z_BUF_ERROR || ret == Z_OK) {
        size_t new_capacity = decompressed_capacity * 2;
        uint8_t *new_decompressed = realloc(decompressed, new_capacity);
        if (!new_decompressed) {
            inflateEnd(&stream);
            free(decompressed);
            return -1;
        }

        decompressed = new_decompressed;
        stream.next_out = decompressed + decompressed_capacity;
        stream.avail_out = (uInt)(new_capacity - decompressed_capacity);  /* Cast size_t to uInt for zlib */
        decompressed_capacity = new_capacity;

        ret = inflate(&stream, Z_FINISH);
    }

    if (ret != Z_STREAM_END) {
        inflateEnd(&stream);
        free(decompressed);
        return -1;
    }

    size_t decompressed_size = stream.total_out;
    inflateEnd(&stream);

    /* Replace compressed body with decompressed */
    free(response->body);
    response->body = decompressed;
    response->body_len = decompressed_size;
    response->body_capacity = decompressed_capacity;

    return 0;
}

/**
 * Execute a synchronous HTTP request
 */
httpmorph_response_t* httpmorph_request_execute(
    httpmorph_client_t *client,
    const httpmorph_request_t *request,
    httpmorph_pool_t *pool) {

    if (!client || !request || !request->url) {
        return NULL;
    }

    httpmorph_response_t *response = response_create();
    if (!response) {
        return NULL;
    }

    uint64_t start_time = get_time_us();
    int sockfd = -1;
    SSL *ssl = NULL;
    char *proxy_user = NULL;
    char *proxy_pass = NULL;
    pooled_connection_t *pooled_conn = NULL;  /* Track if we got connection from pool */
    bool use_http2 = false;  /* Track if HTTP/2 is being used */

    /* Parse URL */
    char *scheme = NULL, *host = NULL, *path = NULL;
    uint16_t port = 0;

    if (parse_url(request->url, &scheme, &host, &port, &path) != 0) {
        response->error = HTTPMORPH_ERROR_PARSE;
        response->error_message = strdup("Failed to parse URL");
        goto cleanup;
    }

    bool use_tls = (strcmp(scheme, "https") == 0);

    /* 1. TCP Connection (direct or via proxy) */
    uint64_t connect_time = 0;

    if (request->proxy_url) {
        /* Connect via proxy */
        char *proxy_host = NULL;
        uint16_t proxy_port = 0;

        /* Parse proxy URL */
        if (parse_proxy_url(request->proxy_url, &proxy_host, &proxy_port,
                           &proxy_user, &proxy_pass) != 0) {
            response->error = HTTPMORPH_ERROR_INVALID_PARAM;
            response->error_message = strdup("Invalid proxy URL");
            goto cleanup;
        }

        /* Override with explicit credentials if provided */
        if (request->proxy_username) {
            free(proxy_user);
            proxy_user = strdup(request->proxy_username);
        }
        if (request->proxy_password) {
            free(proxy_pass);
            proxy_pass = strdup(request->proxy_password);
        }

        /* Connect to proxy server */
        sockfd = tcp_connect(proxy_host, proxy_port, request->timeout_ms, &connect_time);
        if (sockfd < 0) {
            free(proxy_host);
            free(proxy_user);
            free(proxy_pass);
            proxy_user = NULL;
            proxy_pass = NULL;
            response->error = HTTPMORPH_ERROR_NETWORK;
            response->error_message = strdup("Failed to connect to proxy");
            goto cleanup;
        }

        /* For HTTPS, send CONNECT request */
        if (use_tls) {
            if (proxy_connect(sockfd, host, port, proxy_user, proxy_pass,
                            request->timeout_ms) != 0) {
                free(proxy_host);
                free(proxy_user);
                free(proxy_pass);
                proxy_user = NULL;
                proxy_pass = NULL;
                response->error = HTTPMORPH_ERROR_NETWORK;
                response->error_message = strdup("Proxy CONNECT failed");
                goto cleanup;
            }
        }

        free(proxy_host);
        /* Keep proxy_user and proxy_pass for HTTP proxy requests - will be freed later */
    } else {
        /* Direct connection - try pool first for connection reuse */
        if (pool) {
            pooled_conn = pool_get_connection(pool, host, port);
            if (pooled_conn) {
                /* Reuse existing connection from pool */
                sockfd = pooled_conn->sockfd;
                ssl = pooled_conn->ssl;
                use_http2 = pooled_conn->is_http2;  /* Use same protocol as pooled connection */

                /* Don't reuse HTTP/2 connections - HTTP/2 pooling has reliability issues */
                if (use_http2) {
                    pool_connection_destroy(pooled_conn);
                    pooled_conn = NULL;
                    sockfd = -1;
                    ssl = NULL;
                    use_http2 = false;
                }

                /* For SSL connections, verify still valid before reuse */
                if (ssl) {
                    int shutdown_state = SSL_get_shutdown(ssl);
                    if (shutdown_state != 0) {
                        /* SSL was shut down - destroy and recreate */
                        pool_connection_destroy(pooled_conn);
                        pooled_conn = NULL;
                        sockfd = -1;
                        ssl = NULL;
                    }
                }

                if (sockfd >= 0) {
                    /* Connection reused - no connect/TLS time */
                    connect_time = 0;
                    response->tls_time_us = 0;

                    /* Restore TLS info from pooled connection */
                    if (pooled_conn->ja3_fingerprint) {
                        response->ja3_fingerprint = strdup(pooled_conn->ja3_fingerprint);
                    }
                }
            } else {
            }
        }

        /* If no pooled connection, create new one */
        if (sockfd < 0) {
            sockfd = tcp_connect(host, port, request->timeout_ms, &connect_time);
            if (sockfd < 0) {
                response->error = HTTPMORPH_ERROR_NETWORK;
                response->error_message = strdup("Failed to connect");
                goto cleanup;
            }
        }
    }
    response->connect_time_us = connect_time;

    /* 2. TLS Handshake (if HTTPS and not reused) */
    if (use_tls && !ssl) {
        uint64_t tls_time = 0;
        ssl = tls_connect(client->ssl_ctx, sockfd, host, client->browser_profile,
                         request->http2_enabled, &tls_time);
        if (!ssl) {
            response->error = HTTPMORPH_ERROR_TLS;
            response->error_message = strdup("TLS handshake failed");
            goto cleanup;
        }
        response->tls_time_us = tls_time;

        /* Calculate JA3 fingerprint (only for new connections) */
        response->ja3_fingerprint = calculate_ja3_fingerprint(ssl, client->browser_profile);

        /* Check negotiated ALPN protocol */
        const unsigned char *alpn_data = NULL;
        unsigned int alpn_len = 0;
        SSL_get0_alpn_selected(ssl, &alpn_data, &alpn_len);

        if (alpn_data && alpn_len > 0) {
            if (alpn_len == 2 && memcmp(alpn_data, "h2", 2) == 0) {
                /* HTTP/2 negotiated */
                response->http_version = HTTPMORPH_VERSION_2_0;
                use_http2 = true;

                /* Set socket to non-blocking for HTTP/2 */
                if (sockfd != -1) {
#ifdef _WIN32
                    u_long mode = 1;
                    ioctlsocket(sockfd, FIONBIO, &mode);
#else
                    int flags = fcntl(sockfd, F_GETFL, 0);
                    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
#endif
                }
            } else if (alpn_len == 8 && memcmp(alpn_data, "http/1.1", 8) == 0) {
                /* HTTP/1.1 negotiated */
                response->http_version = HTTPMORPH_VERSION_1_1;
            } else if (alpn_len == 8 && memcmp(alpn_data, "http/1.0", 8) == 0) {
                /* HTTP/1.0 negotiated */
                response->http_version = HTTPMORPH_VERSION_1_0;
            }
        }
    }

    /* Extract TLS info for all HTTPS connections (new and reused) */
    if (use_tls && ssl) {
        const SSL_CIPHER *cipher = SSL_get_current_cipher(ssl);
        if (cipher) {
            response->tls_cipher = strdup(SSL_CIPHER_get_name(cipher));
        }
        response->tls_version = strdup(SSL_get_version(ssl));
    }

#ifdef HAVE_NGHTTP2
    /* Use HTTP/2 if negotiated (for both new and pooled connections) */
    if (use_http2) {
        uint64_t first_byte_time = get_time_us();
        if (http2_request(ssl, request, host, path, response) != 0) {
            response->error = HTTPMORPH_ERROR_NETWORK;
            response->error_message = strdup("HTTP/2 request failed");
            goto cleanup;
        }
        response->first_byte_time_us = first_byte_time - start_time;
        goto http2_done;
    }
#endif

    /* 3. Send HTTP/1.x Request */
    bool using_proxy = (request->proxy_url != NULL);
    if (send_http_request(ssl, sockfd, request, host, path, scheme, port, using_proxy, proxy_user, proxy_pass) != 0) {
        /* If send failed on a pooled connection, retry with fresh connection */
        if (pooled_conn) {
            /* Destroy the stale pooled connection */
            pool_connection_destroy(pooled_conn);
            pooled_conn = NULL;
            sockfd = -1;
            ssl = NULL;

            /* Create new connection */
            sockfd = tcp_connect(host, port, request->timeout_ms, &connect_time);
            if (sockfd < 0) {
                response->error = HTTPMORPH_ERROR_NETWORK;
                response->error_message = strdup("Failed to connect after retry");
                goto cleanup;
            }
            response->connect_time_us = connect_time;

            /* New TLS handshake if needed */
            if (use_tls) {
                uint64_t tls_time = 0;
                ssl = tls_connect(client->ssl_ctx, sockfd, host, client->browser_profile,
                                request->http2_enabled, &tls_time);
                if (!ssl) {
                    response->error = HTTPMORPH_ERROR_TLS;
                    response->error_message = strdup("TLS handshake failed on retry");
                    goto cleanup;
                }
                response->tls_time_us = tls_time;
            }

            /* Retry sending request with fresh connection */
            if (send_http_request(ssl, sockfd, request, host, path, scheme, port, using_proxy, proxy_user, proxy_pass) != 0) {
                response->error = HTTPMORPH_ERROR_NETWORK;
                response->error_message = strdup("Failed to send request after retry");
                goto cleanup;
            }
        } else {
            /* Not a pooled connection, fail immediately */
            response->error = HTTPMORPH_ERROR_NETWORK;
            response->error_message = strdup("Failed to send request");
            goto cleanup;
        }
    }

    /* 4. Receive HTTP/1.x Response */
    uint64_t first_byte_time = 0;
    bool connection_will_close = false;
    int recv_result = recv_http_response(ssl, sockfd, response, &first_byte_time, &connection_will_close, request->method);

    /* If pooled connection failed, retry with new connection */
    if (recv_result != 0 && pooled_conn) {

        /* Destroy the failed pooled connection */
        pool_connection_destroy(pooled_conn);
        pooled_conn = NULL;
        sockfd = -1;
        ssl = NULL;

        /* Create new connection */
        sockfd = tcp_connect(host, port, request->timeout_ms, &connect_time);
        if (sockfd < 0) {
            response->error = HTTPMORPH_ERROR_NETWORK;
            response->error_message = strdup("Failed to connect");
            goto cleanup;
        }
        response->connect_time_us = connect_time;

        /* New TLS handshake */
        if (use_tls) {
            uint64_t tls_time = 0;
            ssl = tls_connect(client->ssl_ctx, sockfd, host, client->browser_profile,
                             request->http2_enabled, &tls_time);
            if (!ssl) {
                response->error = HTTPMORPH_ERROR_TLS;
                response->error_message = strdup("TLS handshake failed");
                goto cleanup;
            }
            response->tls_time_us = tls_time;
        }

        /* Reset response completely for retry */
        for (size_t i = 0; i < response->header_count; i++) {
            free(response->header_keys[i]);
            free(response->header_values[i]);
        }
        response->header_count = 0;
        response->body_len = 0;
        response->status_code = 0;

        /* Resend HTTP request */
        if (send_http_request(ssl, sockfd, request, host, path, scheme, port, using_proxy, proxy_user, proxy_pass) != 0) {
            response->error = HTTPMORPH_ERROR_NETWORK;
            response->error_message = strdup("Failed to send request");
            goto cleanup;
        }

        /* Retry receiving response */
        recv_result = recv_http_response(ssl, sockfd, response, &first_byte_time, &connection_will_close, request->method);
    }

    if (recv_result == 0) {
    }

    if (recv_result != 0) {
        response->error = recv_result;
        if (recv_result == HTTPMORPH_ERROR_TIMEOUT) {
            response->error_message = strdup("Request timed out");
        } else {
            response->error_message = strdup("Failed to receive response");
        }
        goto cleanup;
    }
    response->first_byte_time_us = first_byte_time - start_time;

#ifdef HAVE_NGHTTP2
http2_done:
#endif

    /* 5. Decompress gzip if Content-Encoding: gzip or body starts with gzip magic bytes */
    {
        const char *content_encoding = httpmorph_response_get_header(response, "Content-Encoding");
        bool should_decompress = false;

    if (content_encoding && strstr(content_encoding, "gzip")) {
        should_decompress = true;
    }

    /* Also check for gzip magic bytes (0x1f 0x8b) */
    if (!should_decompress && response->body_len >= 2 &&
        response->body[0] == 0x1f && response->body[1] == 0x8b) {
        should_decompress = true;
    }

    if (should_decompress) {
        decompress_gzip(response);
    }

        /* 6. Check if total time exceeded timeout */
        uint64_t elapsed_us = get_time_us() - start_time;
        uint64_t timeout_us = (uint64_t)request->timeout_ms * 1000;
        if (elapsed_us > timeout_us) {
            response->error = HTTPMORPH_ERROR_TIMEOUT;
            response->error_message = strdup("Request timed out");
        } else {
            response->error = HTTPMORPH_OK;
        }
    }

cleanup:
    /* Handle connection pooling */
    if (pool && sockfd >= 0 && response->error == HTTPMORPH_OK) {
        /* Check if server requested connection close or we read until EOF */
        bool should_close = connection_will_close;
        const char *conn_header = httpmorph_response_get_header(response, "Connection");
        if (conn_header && (strstr(conn_header, "close") || strstr(conn_header, "Close"))) {
            should_close = true;
        }
        if (connection_will_close) {
        }

        if (should_close) {
            /* Server wants to close - don't pool */
            if (pooled_conn) {
                pool_connection_destroy(pooled_conn);
                pooled_conn = NULL;
            }
            /* Let normal cleanup close the connection */
        } else {
            /* Request succeeded - pool the connection for reuse */
            pooled_connection_t *conn_to_pool = pooled_conn;

        if (!conn_to_pool) {
            /* New connection - create wrapper */
            bool use_http2 = (response->http_version == HTTPMORPH_VERSION_2_0);
            /* Don't pool HTTP/2 connections - HTTP/2 pooling has reliability issues */
            if (use_http2) {
                conn_to_pool = NULL;
            } else {
                conn_to_pool = pool_connection_create(host, port, sockfd, ssl, use_http2);
                /* Store TLS info in pooled connection for future reuse */
                if (conn_to_pool && ssl) {
                    if (response->ja3_fingerprint) {
                        conn_to_pool->ja3_fingerprint = strdup(response->ja3_fingerprint);
                    }
                    if (response->tls_version) {
                        conn_to_pool->tls_version = strdup(response->tls_version);
                    }
                    if (response->tls_cipher) {
                        conn_to_pool->tls_cipher = strdup(response->tls_cipher);
                    }
                }
            }
        }

        if (conn_to_pool && pool_put_connection(pool, conn_to_pool)) {
            /* Connection successfully pooled - don't close it */
            sockfd = -1;
            ssl = NULL;
            pooled_conn = NULL;  /* Clear so we don't double-free */
        } else if (conn_to_pool || pooled_conn) {
            /* Failed to pool connection - pool_put_connection already destroyed it */
            /* Just clear our references to avoid double-free */
            sockfd = -1;
            ssl = NULL;
            pooled_conn = NULL;
        }
        }  /* End of should_close else block */
    } else if (pooled_conn) {
        /* Request failed - destroy the pooled connection */
        pool_connection_destroy(pooled_conn);
        sockfd = -1;
        ssl = NULL;
        pooled_conn = NULL;
    }

    /* Only close if not pooled */
    if (ssl) {
        /* Skip SSL_shutdown() as it can block indefinitely on stale/proxy connections.
         * SSL_free() will handle cleanup safely without blocking. */
        SSL_free(ssl);
    }
    if (sockfd >= 0) {
        close(sockfd);
    }

    free(scheme);
    free(host);
    free(path);
    free(proxy_user);
    free(proxy_pass);

    response->total_time_us = get_time_us() - start_time;

    return response;
}

/**
 * Create a new request
 */
httpmorph_request_t* httpmorph_request_create(httpmorph_method_t method,
                                              const char *url) {
    if (!url) {
        return NULL;
    }

    httpmorph_request_t *request = calloc(1, sizeof(httpmorph_request_t));
    if (!request) {
        return NULL;
    }

    request->method = method;
    request->url = strdup(url);
    request->timeout_ms = 30000;  /* Default 30 seconds */
    request->http_version = HTTPMORPH_VERSION_1_1;

    return request;
}

/**
 * Destroy a request
 */
void httpmorph_request_destroy(httpmorph_request_t *request) {
    if (!request) {
        return;
    }

    free(request->url);
    free(request->host);
    free(request->browser_version);
    free(request->proxy_url);
    free(request->ja3_string);
    free(request->user_agent);

    /* Free headers */
    for (size_t i = 0; i < request->header_count; i++) {
        free(request->header_keys[i]);
        free(request->header_values[i]);
    }
    free(request->header_keys);
    free(request->header_values);

    /* Free body */
    free(request->body);

    free(request);
}

/**
 * Add header to request
 */
int httpmorph_request_add_header(httpmorph_request_t *request,
                                 const char *key, const char *value) {
    if (!request || !key || !value) {
        return -1;
    }

    size_t new_count = request->header_count + 1;

    char **new_keys = realloc(request->header_keys, new_count * sizeof(char*));
    char **new_values = realloc(request->header_values, new_count * sizeof(char*));

    if (!new_keys || !new_values) {
        return -1;
    }

    new_keys[request->header_count] = strdup(key);
    new_values[request->header_count] = strdup(value);

    request->header_keys = new_keys;
    request->header_values = new_values;
    request->header_count = new_count;

    return 0;
}

/**
 * Set request body
 */
int httpmorph_request_set_body(httpmorph_request_t *request,
                               const uint8_t *body, size_t body_len) {
    if (!request || !body) {
        return -1;
    }

    /* Free existing body */
    free(request->body);

    /* Allocate new body */
    request->body = malloc(body_len);
    if (!request->body) {
        return -1;
    }

    memcpy(request->body, body, body_len);
    request->body_len = body_len;

    return 0;
}

/**
 * Set request timeout in milliseconds
 */
void httpmorph_request_set_timeout(httpmorph_request_t *request,
                                   uint32_t timeout_ms) {
    if (request) {
        request->timeout_ms = timeout_ms;
    }
}

void httpmorph_request_set_proxy(httpmorph_request_t *request,
                                 const char *proxy_url,
                                 const char *username,
                                 const char *password) {
    if (!request) return;

    if (request->proxy_url) {
        free(request->proxy_url);
        request->proxy_url = NULL;
    }
    if (request->proxy_username) {
        free(request->proxy_username);
        request->proxy_username = NULL;
    }
    if (request->proxy_password) {
        free(request->proxy_password);
        request->proxy_password = NULL;
    }

    if (proxy_url) {
        request->proxy_url = strdup(proxy_url);
    }
    if (username) {
        request->proxy_username = strdup(username);
    }
    if (password) {
        request->proxy_password = strdup(password);
    }
}

/**
 * Set HTTP/2 enabled flag
 */
void httpmorph_request_set_http2(httpmorph_request_t *request, bool enabled) {
    if (request) {
        request->http2_enabled = enabled;
    }
}

/**
 * Get response header value
 */
const char* httpmorph_response_get_header(const httpmorph_response_t *response,
                                          const char *key) {
    if (!response || !key) {
        return NULL;
    }

    for (size_t i = 0; i < response->header_count; i++) {
        if (strcasecmp(response->header_keys[i], key) == 0) {
            return response->header_values[i];
        }
    }

    return NULL;
}

/**
 * Execute request within session
 */
httpmorph_response_t* httpmorph_session_request(httpmorph_session_t *session,
                                                const httpmorph_request_t *request) {
    if (!session || !request) {
        return NULL;
    }

    /* Parse URL to extract host for cookie domain */
    char *scheme = NULL, *host = NULL, *path = NULL;
    uint16_t port = 0;
    int parse_result = parse_url(request->url, &scheme, &host, &port, &path);
    if (parse_result != 0 || !host) {
        /* If we can't parse URL, still execute request but cookies won't work */
        host = strdup("unknown");
    }

    /* Add cookies from jar to request */
    char *cookie_header = get_cookies_for_request(session, host,
                                                   path ? path : "/",
                                                   request->use_tls);

    /* Create a mutable copy of the request to add cookie header */
    httpmorph_request_t *req_with_cookies = (httpmorph_request_t*)request;

    if (cookie_header) {
        /* Add Cookie header to request */
        httpmorph_request_add_header(req_with_cookies, "Cookie", cookie_header);
        free(cookie_header);
    }

    /* Execute the request with connection pooling */
    httpmorph_response_t *response = httpmorph_request_execute(session->client, req_with_cookies, session->pool);

    /* Parse Set-Cookie headers from response */
    if (response) {
        for (size_t i = 0; i < response->header_count; i++) {
            if (strcasecmp(response->header_keys[i], "Set-Cookie") == 0) {
                parse_set_cookie(session, response->header_values[i], host);
            }
        }
    }

    /* Cleanup parsed URL components */
    free(scheme);
    free(host);
    free(path);

    return response;
}

/**
 * Get cookie count for session
 */
size_t httpmorph_session_cookie_count(httpmorph_session_t *session) {
    if (!session) {
        return 0;
    }
    return session->cookie_count;
}
