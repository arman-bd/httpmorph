/**
 * httpmorph.h - Main header file for httpmorph
 *
 * Morph into any browser - High-performance HTTP/HTTPS client library
 * with dynamic browser fingerprinting
 */

#ifndef HTTPMORPH_H
#define HTTPMORPH_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Version information */
#define HTTPMORPH_VERSION_MAJOR 0
#define HTTPMORPH_VERSION_MINOR 1
#define HTTPMORPH_VERSION_PATCH 0

/* Error codes */
typedef enum {
    HTTPMORPH_OK = 0,
    HTTPMORPH_ERROR_MEMORY = -1,
    HTTPMORPH_ERROR_INVALID_PARAM = -2,
    HTTPMORPH_ERROR_NETWORK = -3,
    HTTPMORPH_ERROR_TLS = -4,
    HTTPMORPH_ERROR_TIMEOUT = -5,
    HTTPMORPH_ERROR_PARSE = -6,
    HTTPMORPH_ERROR_PROTOCOL = -7,
} httpmorph_error_t;

/* HTTP methods */
typedef enum {
    HTTPMORPH_GET,
    HTTPMORPH_POST,
    HTTPMORPH_PUT,
    HTTPMORPH_DELETE,
    HTTPMORPH_HEAD,
    HTTPMORPH_OPTIONS,
    HTTPMORPH_PATCH,
    HTTPMORPH_CONNECT,
} httpmorph_method_t;

/* HTTP version */
typedef enum {
    HTTPMORPH_VERSION_1_0,
    HTTPMORPH_VERSION_1_1,
    HTTPMORPH_VERSION_2_0,
    HTTPMORPH_VERSION_3_0,
} httpmorph_version_t;

/* Browser types for fingerprinting */
typedef enum {
    HTTPMORPH_BROWSER_CHROME,
    HTTPMORPH_BROWSER_FIREFOX,
    HTTPMORPH_BROWSER_SAFARI,
    HTTPMORPH_BROWSER_EDGE,
    HTTPMORPH_BROWSER_RANDOM,
    HTTPMORPH_BROWSER_CUSTOM,
} httpmorph_browser_t;

/* Forward declarations */
typedef struct httpmorph_client httpmorph_client_t;
typedef struct httpmorph_request httpmorph_request_t;
typedef struct httpmorph_response httpmorph_response_t;
typedef struct httpmorph_session httpmorph_session_t;
typedef struct httpmorph_pool httpmorph_pool_t;

/* Request structure */
struct httpmorph_request {
    httpmorph_method_t method;
    char *url;
    char *host;
    uint16_t port;
    bool use_tls;

    /* Headers */
    char **header_keys;
    char **header_values;
    size_t header_count;

    /* Body */
    uint8_t *body;
    size_t body_len;

    /* Configuration */
    uint32_t timeout_ms;
    httpmorph_version_t http_version;
    httpmorph_browser_t browser_type;
    char *browser_version;
    bool rotate_fingerprint;

    /* Proxy */
    char *proxy_url;
    char *proxy_username;
    char *proxy_password;

    /* HTTP/2 control */
    bool http2_enabled;

    /* TLS fingerprinting */
    char *ja3_string;
    char *user_agent;
};

/* Response structure */
struct httpmorph_response {
    uint16_t status_code;
    httpmorph_version_t http_version;

    /* Headers */
    char **header_keys;
    char **header_values;
    size_t header_count;

    /* Body */
    uint8_t *body;
    size_t body_len;
    size_t body_capacity;

    /* Timing */
    uint64_t connect_time_us;
    uint64_t tls_time_us;
    uint64_t first_byte_time_us;
    uint64_t total_time_us;

    /* TLS info */
    char *tls_version;
    char *tls_cipher;
    char *ja3_fingerprint;

    /* Error */
    httpmorph_error_t error;
    char *error_message;
};

/* Core API */

/**
 * Initialize the httpmorph library
 * Must be called before any other functions
 */
int httpmorph_init(void);

/**
 * Cleanup the httpmorph library
 */
void httpmorph_cleanup(void);

/**
 * Get library version string
 */
const char* httpmorph_version(void);

/* Client API */

/**
 * Create a new HTTP client
 */
httpmorph_client_t* httpmorph_client_create(void);

/**
 * Get the connection pool from a client
 */
httpmorph_pool_t* httpmorph_client_get_pool(httpmorph_client_t *client);

/**
 * Destroy an HTTP client
 */
void httpmorph_client_destroy(httpmorph_client_t *client);

/**
 * Execute a synchronous HTTP request
 * @param pool Optional connection pool for connection reuse (pass NULL if not using pooling)
 */
httpmorph_response_t* httpmorph_request_execute(
    httpmorph_client_t *client,
    const httpmorph_request_t *request,
    httpmorph_pool_t *pool
);

/* Request helpers */

/**
 * Create a new request
 */
httpmorph_request_t* httpmorph_request_create(
    httpmorph_method_t method,
    const char *url
);

/**
 * Destroy a request
 */
void httpmorph_request_destroy(httpmorph_request_t *request);

/**
 * Add header to request
 */
int httpmorph_request_add_header(
    httpmorph_request_t *request,
    const char *key,
    const char *value
);

/**
 * Set request body
 */
int httpmorph_request_set_body(
    httpmorph_request_t *request,
    const uint8_t *body,
    size_t body_len
);

/**
 * Set request timeout in milliseconds
 */
void httpmorph_request_set_timeout(
    httpmorph_request_t *request,
    uint32_t timeout_ms
);

/**
 * Set proxy for request
 */
void httpmorph_request_set_proxy(
    httpmorph_request_t *request,
    const char *proxy_url,
    const char *username,
    const char *password
);

/**
 * Set HTTP/2 enabled flag for request
 */
void httpmorph_request_set_http2(
    httpmorph_request_t *request,
    bool enabled
);

/* Response helpers */

/**
 * Destroy a response
 */
void httpmorph_response_destroy(httpmorph_response_t *response);

/**
 * Get response header value
 */
const char* httpmorph_response_get_header(
    const httpmorph_response_t *response,
    const char *key
);

/* Session API (for persistent connections and fingerprints) */

/**
 * Create a new session
 */
httpmorph_session_t* httpmorph_session_create(
    httpmorph_browser_t browser_type
);

/**
 * Destroy a session
 */
void httpmorph_session_destroy(httpmorph_session_t *session);

/**
 * Execute request within session
 */
httpmorph_response_t* httpmorph_session_request(
    httpmorph_session_t *session,
    const httpmorph_request_t *request
);

/**
 * Get cookie count for session
 */
size_t httpmorph_session_cookie_count(httpmorph_session_t *session);

#ifdef __cplusplus
}
#endif

#endif /* HTTPMORPH_H */
