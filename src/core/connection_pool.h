/**
 * connection_pool.h - HTTP connection pooling for keep-alive
 *
 * Implements connection reuse to avoid repeated TCP + TLS handshakes.
 * Each pool entry caches a socket + SSL connection for a specific host:port.
 */

#ifndef HTTPMORPH_CONNECTION_POOL_H
#define HTTPMORPH_CONNECTION_POOL_H

#include <stdbool.h>
#include <time.h>
#include <openssl/ssl.h>

/* Forward declarations */
typedef struct httpmorph_pool httpmorph_pool_t;
typedef struct pooled_connection pooled_connection_t;

/* Configuration constants */
#define POOL_MAX_CONNECTIONS_PER_HOST 6    /* Match browser behavior */
#define POOL_MAX_TOTAL_CONNECTIONS 100     /* Global limit */
#define POOL_IDLE_TIMEOUT_SECONDS 30       /* Close after 30s idle */
#define POOL_MAX_HOST_KEY_LEN 256          /* "hostname:port" max length */

/* Connection state (following httpcore's pattern) */
typedef enum {
    POOL_CONN_IDLE = 0,      /* Connection available for reuse */
    POOL_CONN_ACTIVE = 1,    /* Connection currently processing request */
    POOL_CONN_CLOSED = 2     /* Connection closed/invalid */
} pool_connection_state_t;

/**
 * Pooled connection structure
 * Represents a reusable connection to a specific host:port
 */
struct pooled_connection {
    /* Connection identifiers */
    char host_key[POOL_MAX_HOST_KEY_LEN];  /* "hostname:port" */

    /* Socket and SSL */
    int sockfd;
    SSL *ssl;                               /* NULL for HTTP connections */

    /* State tracking */
    time_t last_used;                       /* Unix timestamp */
    bool is_http2;                          /* HTTP/2 connection */
    bool is_valid;                          /* Connection still alive */
    bool preface_sent;                      /* HTTP/2 preface already sent on this connection */
    pool_connection_state_t state;          /* Current connection state */

    /* TLS fingerprinting info (for HTTPS connections) */
    char *ja3_fingerprint;                  /* JA3 fingerprint from initial handshake */
    char *tls_version;                      /* TLS version string */
    char *tls_cipher;                       /* TLS cipher suite name */

#ifdef HAVE_NGHTTP2
    /* HTTP/2 session (only if is_http2 is true) */
    void *http2_session;                    /* nghttp2_session* */
    void *http2_stream_data;                /* http2_stream_data_t* - persistent callback data */
#endif

    /* Linked list */
    pooled_connection_t *next;
};

/**
 * Connection pool structure
 * Manages a collection of reusable connections
 */
struct httpmorph_pool {
    /* Connection storage (simple linked list for now) */
    pooled_connection_t *connections;

    /* Statistics */
    int total_connections;
    int active_connections;

    /* Configuration */
    int max_connections_per_host;
    int max_total_connections;
    int idle_timeout_seconds;
};

/* === Pool Management === */

/**
 * Create a new connection pool
 */
httpmorph_pool_t* pool_create(void);

/**
 * Destroy a connection pool and close all connections
 */
void pool_destroy(httpmorph_pool_t *pool);

/**
 * Clean up idle connections (older than timeout)
 * Called periodically to free resources
 */
void pool_cleanup_idle(httpmorph_pool_t *pool);

/* === Connection Operations === */

/**
 * Get a connection from the pool
 * Returns an existing connection if available, NULL otherwise
 *
 * @param pool The connection pool
 * @param host Hostname (e.g., "example.com")
 * @param port Port number (e.g., 443)
 * @return Pooled connection or NULL if not found
 */
pooled_connection_t* pool_get_connection(httpmorph_pool_t *pool,
                                        const char *host,
                                        int port);

/**
 * Return a connection to the pool for reuse
 * If pool is full or connection is invalid, it will be closed
 *
 * @param pool The connection pool
 * @param conn The connection to return (takes ownership)
 * @return true if connection was pooled, false if it was closed
 */
bool pool_put_connection(httpmorph_pool_t *pool, pooled_connection_t *conn);

/**
 * Create a pooled connection wrapper
 * Does NOT add to pool yet - call pool_put_connection() for that
 *
 * @param host Hostname
 * @param port Port number
 * @param sockfd Socket file descriptor
 * @param ssl SSL connection (or NULL for HTTP)
 * @param is_http2 Whether this is an HTTP/2 connection
 * @return New pooled connection (caller must free or pool it)
 */
pooled_connection_t* pool_connection_create(const char *host,
                                           int port,
                                           int sockfd,
                                           SSL *ssl,
                                           bool is_http2);

/**
 * Close and free a pooled connection
 * Closes socket, frees SSL, frees memory
 */
void pool_connection_destroy(pooled_connection_t *conn);

/**
 * Validate a pooled connection
 * Checks if the socket is still alive and usable
 *
 * @param conn Connection to validate
 * @return true if connection is still valid, false otherwise
 */
bool pool_connection_validate(pooled_connection_t *conn);

/* === Helper Functions === */

/**
 * Build a host key for lookup
 * Format: "hostname:port"
 *
 * @param host Hostname
 * @param port Port number
 * @param key_out Buffer to write key (must be POOL_MAX_HOST_KEY_LEN bytes)
 */
void pool_build_host_key(const char *host, int port, char *key_out);

/**
 * Count connections for a specific host
 *
 * @param pool The connection pool
 * @param host_key Host key ("hostname:port")
 * @return Number of connections for this host
 */
int pool_count_connections_for_host(httpmorph_pool_t *pool, const char *host_key);

#endif /* HTTPMORPH_CONNECTION_POOL_H */
