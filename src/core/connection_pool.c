/**
 * connection_pool.c - HTTP connection pooling implementation
 *
 * Implements connection reuse for HTTP keep-alive.
 */

#include "connection_pool.h"
#include "internal/network.h"
#include "internal/tls.h"
#include "internal/client.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef HAVE_NGHTTP2
    #include <nghttp2/nghttp2.h>
    #include "http2_session_manager.h"
#endif

/* Platform-specific includes */
#ifdef _WIN32
    #include <winsock2.h>
    #include <windows.h>
    #define close closesocket
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <fcntl.h>
    #include <errno.h>
    #include <pthread.h>
#endif

#include <openssl/ssl.h>

/* === Pool Management === */

httpmorph_pool_t* pool_create(void) {
    httpmorph_pool_t *pool = (httpmorph_pool_t*)calloc(1, sizeof(httpmorph_pool_t));
    if (!pool) {
        return NULL;
    }

    pool->connections = NULL;
    pool->total_connections = 0;
    pool->active_connections = 0;
    pool->max_connections_per_host = POOL_MAX_CONNECTIONS_PER_HOST;
    pool->max_total_connections = POOL_MAX_TOTAL_CONNECTIONS;
    pool->idle_timeout_seconds = POOL_IDLE_TIMEOUT_SECONDS;

    /* Initialize mutex for thread safety */
#ifdef _WIN32
    CRITICAL_SECTION *cs = (CRITICAL_SECTION*)malloc(sizeof(CRITICAL_SECTION));
    if (cs) {
        InitializeCriticalSection(cs);
        pool->mutex = cs;
    } else {
        free(pool);
        return NULL;
    }
#else
    pthread_mutex_t *mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
    if (mutex) {
        if (pthread_mutex_init(mutex, NULL) != 0) {
            free(mutex);
            free(pool);
            return NULL;
        }
        pool->mutex = mutex;
    } else {
        free(pool);
        return NULL;
    }
#endif

    return pool;
}

void pool_destroy(httpmorph_pool_t *pool) {
    if (!pool) {
        return;
    }

    /* Lock mutex before destroying to prevent concurrent access */
    void *mutex_ptr = pool->mutex;
#ifdef _WIN32
    CRITICAL_SECTION *cs = (CRITICAL_SECTION*)mutex_ptr;
    if (cs) {
        EnterCriticalSection(cs);
    }
#else
    pthread_mutex_t *mutex = (pthread_mutex_t*)mutex_ptr;
    if (mutex) {
        pthread_mutex_lock(mutex);
    }
#endif

    /* Close and free all connections while holding lock
     * BUT only if they're not still in use (ref_count == 0)
     * If ref_count > 0, the connection is still being used by another thread
     * and we should NOT destroy it to avoid use-after-free bugs */
    pooled_connection_t *conn = pool->connections;
    while (conn) {
        pooled_connection_t *next = conn->next;

        /* Only destroy if not in use */
        if (conn->ref_count == 0) {
            pool_connection_destroy(conn);
        }
        /* If ref_count > 0, leak it rather than crash - the OS will clean up on exit */

        conn = next;
    }
    pool->connections = NULL;

    /* Unlock before destroying mutex */
#ifdef _WIN32
    if (cs) {
        LeaveCriticalSection(cs);
        DeleteCriticalSection(cs);
        free(cs);
    }
#else
    if (mutex) {
        pthread_mutex_unlock(mutex);
        pthread_mutex_destroy(mutex);
        free(mutex);
    }
#endif

    pool->mutex = NULL;
    free(pool);
}

void pool_cleanup_idle(httpmorph_pool_t *pool) {
    if (!pool) {
        return;
    }

    /* Lock pool for thread safety */
#ifdef _WIN32
    CRITICAL_SECTION *cs = (CRITICAL_SECTION*)pool->mutex;
    if (cs) EnterCriticalSection(cs);
#else
    pthread_mutex_t *mutex = (pthread_mutex_t*)pool->mutex;
    if (mutex) pthread_mutex_lock(mutex);
#endif

    time_t now = time(NULL);
    pooled_connection_t **curr = &pool->connections;

    while (*curr) {
        pooled_connection_t *conn = *curr;

        /* Check if connection is idle for too long */
        if (now - conn->last_used > pool->idle_timeout_seconds) {
            /* Remove from list */
            *curr = conn->next;
            pool->total_connections--;

            /* Close and free */
            pool_connection_destroy(conn);
        } else {
            /* Move to next */
            curr = &conn->next;
        }
    }

    /* Unlock pool */
#ifdef _WIN32
    if (cs) LeaveCriticalSection(cs);
#else
    if (mutex) pthread_mutex_unlock(mutex);
#endif
}

/* === Connection Operations === */

pooled_connection_t* pool_get_connection(httpmorph_pool_t *pool,
                                        const char *host,
                                        int port) {
    if (!pool || !host) {
        return NULL;
    }

    /* Lock pool for thread safety */
#ifdef _WIN32
    CRITICAL_SECTION *cs = (CRITICAL_SECTION*)pool->mutex;
    if (cs) EnterCriticalSection(cs);
#else
    pthread_mutex_t *mutex = (pthread_mutex_t*)pool->mutex;
    if (mutex) pthread_mutex_lock(mutex);
#endif

    /* Build host key */
    char host_key[POOL_MAX_HOST_KEY_LEN];
    pool_build_host_key(host, port, host_key);

    /* Search for matching connection */
    pooled_connection_t **curr = &pool->connections;
    pooled_connection_t *result = NULL;

    while (*curr) {
        pooled_connection_t *conn = *curr;

        if (strcmp(conn->host_key, host_key) == 0) {
            /* Found matching host - validate it */
            if (pool_connection_validate(conn)) {
                /* HTTP/2 connections can be shared (multiplexing) */
                if (conn->is_http2 && conn->ref_count > 0) {
                    /* Connection already in use - increment ref_count and share it */
                    conn->ref_count++;
                    conn->last_used = time(NULL);
                    result = conn;
                    break;
                }

                /* For HTTP/1.1 or first use of HTTP/2: remove from pool */
                *curr = conn->next;
                pool->total_connections--;
                pool->active_connections++;

                /* Update last used time and increment reference count */
                conn->last_used = time(NULL);
                conn->ref_count = 1;  /* First reference */
                conn->next = NULL;

                result = conn;
                break;
            } else {
                /* Connection is dead - remove and destroy it */
                *curr = conn->next;
                pool->total_connections--;
                pool_connection_destroy(conn);
                /* Continue searching */
            }
        } else {
            /* Move to next */
            curr = &conn->next;
        }
    }

    /* Unlock pool */
#ifdef _WIN32
    if (cs) LeaveCriticalSection(cs);
#else
    if (mutex) pthread_mutex_unlock(mutex);
#endif

    return result;
}

bool pool_put_connection(httpmorph_pool_t *pool, pooled_connection_t *conn) {
    if (!pool || !conn) {
        return false;
    }

    /* Lock pool for thread safety */
#ifdef _WIN32
    CRITICAL_SECTION *cs = (CRITICAL_SECTION*)pool->mutex;
    if (cs) EnterCriticalSection(cs);
#else
    pthread_mutex_t *mutex = (pthread_mutex_t*)pool->mutex;
    if (mutex) pthread_mutex_lock(mutex);
#endif

    /* Skip validation on put - we'll validate on get instead.
     * This saves 4 fcntl() system calls per request. */

    bool result = false;

    /* Decrement reference count */
    if (conn->ref_count > 0) {
        conn->ref_count--;
    }

    /* HTTP/2 connections with remaining references stay in pool (shared) */
    if (conn->is_http2 && conn->ref_count > 0) {
        /* Connection still in use by other requests - keep it shared */
        result = true;
        goto unlock_and_return;
    }

    /* HTTP/1.1 connections or HTTP/2 with no more references: return to pool */

    /* Check if pool is full */
    if (pool->total_connections >= pool->max_total_connections) {
        /* Pool is full - close connection (outside lock) */
        goto unlock_and_destroy;
    }

    /* Check per-host limit */
    int host_conn_count = pool_count_connections_for_host(pool, conn->host_key);
    if (host_conn_count >= pool->max_connections_per_host) {
        /* Too many connections for this host (outside lock) */
        goto unlock_and_destroy;
    }

    /* Add to pool */
    conn->last_used = time(NULL);
    conn->next = pool->connections;
    pool->connections = conn;
    pool->total_connections++;

    if (pool->active_connections > 0) {
        pool->active_connections--;
    }

    result = true;

unlock_and_return:
    /* Unlock pool and return (connection stays in pool) */
#ifdef _WIN32
    if (cs) LeaveCriticalSection(cs);
#else
    if (mutex) pthread_mutex_unlock(mutex);
#endif
    return result;

unlock_and_destroy:
    /* Unlock pool */
#ifdef _WIN32
    if (cs) LeaveCriticalSection(cs);
#else
    if (mutex) pthread_mutex_unlock(mutex);
#endif

    /* Destroy connection if not pooled (outside of lock) */
    if (!result) {
        pool_connection_destroy(conn);
    }

    return result;
}

pooled_connection_t* pool_connection_create(const char *host,
                                           int port,
                                           int sockfd,
                                           SSL *ssl,
                                           bool is_http2) {
    if (!host || sockfd < 0) {
        return NULL;
    }

    /* Ensure socket is in blocking mode for HTTP/1.1 compatibility
     * (HTTP/2 connections are already non-blocking) */
    if (!is_http2) {
#ifdef _WIN32
        u_long mode = 0;  /* 0 = blocking */
        ioctlsocket(sockfd, FIONBIO, &mode);
#else
        int flags = fcntl(sockfd, F_GETFL, 0);
        if (flags != -1) {
            fcntl(sockfd, F_SETFL, flags & ~O_NONBLOCK);
        }
#endif
    }

    pooled_connection_t *conn = (pooled_connection_t*)calloc(1, sizeof(pooled_connection_t));
    if (!conn) {
        return NULL;
    }

    /* Build host key */
    pool_build_host_key(host, port, conn->host_key);

    /* Initialize connection */
    conn->sockfd = sockfd;
    conn->ssl = ssl;
    conn->is_http2 = is_http2;
    conn->is_valid = true;
    conn->preface_sent = false;  /* Will be set to true after first HTTP/2 preface */
    conn->state = POOL_CONN_IDLE;
    conn->ref_count = 0;  /* No references yet */
    conn->last_used = time(NULL);
    conn->next = NULL;

    /* Initialize proxy info fields */
    conn->is_proxy = false;
    conn->proxy_url = NULL;
    conn->target_host = NULL;
    conn->target_port = 0;

    /* Initialize TLS info fields */
    conn->ja3_fingerprint = NULL;
    conn->tls_version = NULL;
    conn->tls_cipher = NULL;

#ifdef HAVE_NGHTTP2
    conn->http2_session = NULL;
    conn->http2_stream_data = NULL;
    conn->http2_session_manager = NULL;
#endif

    return conn;
}

void pool_connection_destroy(pooled_connection_t *conn) {
    if (!conn) {
        return;
    }

#ifdef HAVE_NGHTTP2
    /* Destroy HTTP/2 session manager if present (includes session cleanup) */
    if (conn->http2_session_manager) {
        http2_session_manager_t *mgr = (http2_session_manager_t *)conn->http2_session_manager;
        http2_session_manager_destroy(mgr);
        conn->http2_session_manager = NULL;
    }

    /* Destroy HTTP/2 session if present (for connections without manager) */
    if (conn->http2_session) {
        /* nghttp2_session* */
        nghttp2_session *session = (nghttp2_session *)conn->http2_session;
        nghttp2_session_del(session);
        conn->http2_session = NULL;
    }
    /* Note: http2_stream_data is managed separately and freed when session ends */
#endif

    /* Free proxy info */
    if (conn->proxy_url) {
        free(conn->proxy_url);
        conn->proxy_url = NULL;
    }
    if (conn->target_host) {
        free(conn->target_host);
        conn->target_host = NULL;
    }

    /* Free TLS info */
    if (conn->ja3_fingerprint) {
        free(conn->ja3_fingerprint);
        conn->ja3_fingerprint = NULL;
    }
    if (conn->tls_version) {
        free(conn->tls_version);
        conn->tls_version = NULL;
    }
    if (conn->tls_cipher) {
        free(conn->tls_cipher);
        conn->tls_cipher = NULL;
    }

    /* Close SSL */
    if (conn->ssl) {
        /* Skip SSL_shutdown() as it can block indefinitely on stale/proxy connections.
         * SSL_free() will handle cleanup safely without blocking. */
        SSL_free(conn->ssl);
        conn->ssl = NULL;
    }

    /* Close socket (but never close stdin/stdout/stderr) */
    if (conn->sockfd > 2) {
        close(conn->sockfd);
        conn->sockfd = -1;  /* Mark as closed to prevent double-close */
    }

    free(conn);
}

bool pool_connection_validate(pooled_connection_t *conn) {
    if (!conn || conn->sockfd < 0 || !conn->is_valid) {
        return false;
    }

    /* For SSL connections, just check shutdown state */
    if (conn->ssl) {
        int shutdown_state = SSL_get_shutdown(conn->ssl);
        if (shutdown_state != 0) {
            return false;
        }
        /* Trust the connection - if it fails, we'll handle it during actual use */
        return true;
    }

    /* For non-SSL connections, also just trust them */
    /* The cost of validation (fcntl, recv) is higher than just trying to use
     * the connection and handling failures. If the connection is dead, the
     * next request will fail and we'll create a new one. */
    return true;
}

/* === Helper Functions === */

void pool_build_host_key(const char *host, int port, char *key_out) {
    if (!host || !key_out) {
        return;
    }

    snprintf(key_out, POOL_MAX_HOST_KEY_LEN, "%s:%d", host, port);
}

int pool_count_connections_for_host(httpmorph_pool_t *pool, const char *host_key) {
    if (!pool || !host_key) {
        return 0;
    }

    int count = 0;
    pooled_connection_t *conn = pool->connections;
    while (conn) {
        if (strcmp(conn->host_key, host_key) == 0) {
            count++;
        }
        conn = conn->next;
    }

    return count;
}

/**
 * Pre-warm connections to a host
 * Establishes N connections and adds them to the pool for immediate reuse
 */
int pool_prewarm_connections(httpmorph_pool_t *pool,
                             httpmorph_client_t *client,
                             const char *host,
                             int port,
                             bool use_tls,
                             int count) {
    if (!pool || !client || !host || count <= 0) {
        return 0;
    }

    int created = 0;
    uint16_t actual_port = (port > 0) ? port : (use_tls ? 443 : 80);

    for (int i = 0; i < count; i++) {
        /* Create TCP connection */
        uint64_t connect_time_us = 0;
        int sockfd = httpmorph_tcp_connect(host, actual_port, client->timeout_ms, &connect_time_us);
        if (sockfd < 0) {
            continue;  /* Skip failed connections */
        }

        SSL *ssl = NULL;
        if (use_tls) {
            /* Establish TLS */
            uint64_t tls_time = 0;
            ssl = httpmorph_tls_connect(client->ssl_ctx, sockfd, host,
                                       client->browser_profile,
                                       true, &tls_time);  /* verify_cert = true by default */
            if (!ssl) {
                if (sockfd > 2) close(sockfd);
                continue;  /* Skip failed TLS */
            }
        }

        /* Create pooled connection */
        pooled_connection_t *conn = pool_connection_create(host, actual_port, sockfd, ssl, false);
        if (!conn) {
            if (ssl) SSL_free(ssl);
            if (sockfd > 2) close(sockfd);
            continue;
        }

        /* Add to pool */
        if (pool_put_connection(pool, conn)) {
            created++;
        } else {
            /* Pool rejected connection - clean up and stop */
            pool_connection_destroy(conn);
            break;
        }
    }

    return created;
}

/* === Async I/O Support === */

/**
 * Get file descriptor from a pooled connection
 */
int httpmorph_connection_get_fd(pooled_connection_t *conn) {
    if (!conn) {
        return -1;
    }

    /* Return -1 if connection is closed or invalid */
    if (conn->state == POOL_CONN_CLOSED || !conn->is_valid) {
        return -1;
    }

    /* Return the socket file descriptor */
    return conn->sockfd;
}

/**
 * Get file descriptor from connection pool (public API wrapper)
 */
int httpmorph_pool_get_connection_fd(httpmorph_pool_t *pool,
                                     const char *host,
                                     uint16_t port) {
    if (!pool || !host) {
        return -1;
    }

    /* Build host key */
    char host_key[POOL_MAX_HOST_KEY_LEN];
    pool_build_host_key(host, port, host_key);

    /* Lock pool for thread safety */
#ifdef _WIN32
    CRITICAL_SECTION *cs = (CRITICAL_SECTION*)pool->mutex;
    if (cs) EnterCriticalSection(cs);
#else
    pthread_mutex_t *mutex = (pthread_mutex_t*)pool->mutex;
    if (mutex) pthread_mutex_lock(mutex);
#endif

    /* Find an active connection for this host */
    pooled_connection_t *conn = pool->connections;
    int fd = -1;

    while (conn) {
        if (strcmp(conn->host_key, host_key) == 0) {
            /* Found a connection for this host */
            if (conn->state == POOL_CONN_ACTIVE && conn->is_valid) {
                fd = conn->sockfd;
                break;
            }
        }
        conn = conn->next;
    }

    /* Unlock pool */
#ifdef _WIN32
    if (cs) LeaveCriticalSection(cs);
#else
    if (mutex) pthread_mutex_unlock(mutex);
#endif

    return fd;
}

/**
 * Register callback for socket readable event (Phase A placeholder)
 * In Phase A, event loop integration happens in Python using add_reader/add_writer
 * This function is a stub for future C-level event loop integration (Phase B)
 */
int httpmorph_connection_on_readable(pooled_connection_t *conn,
                                     socket_event_callback_t callback,
                                     void *user_data) {
    if (!conn || !callback) {
        return -1;
    }

    /* Phase A: This is a placeholder
     * Phase B: Will integrate with io_engine to register socket events
     * For now, Python handles event loop integration using asyncio.add_reader()
     */

    (void)user_data;  /* Unused in Phase A */
    return 0;  /* Success (no-op) */
}

/**
 * Register callback for socket writable event (Phase A placeholder)
 */
int httpmorph_connection_on_writable(pooled_connection_t *conn,
                                     socket_event_callback_t callback,
                                     void *user_data) {
    if (!conn || !callback) {
        return -1;
    }

    /* Phase A: This is a placeholder
     * Phase B: Will integrate with io_engine to register socket events
     * For now, Python handles event loop integration using asyncio.add_writer()
     */

    (void)user_data;  /* Unused in Phase A */
    return 0;  /* Success (no-op) */
}
