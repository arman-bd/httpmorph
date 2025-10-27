/**
 * connection_pool.c - HTTP connection pooling implementation
 *
 * Implements connection reuse for HTTP keep-alive.
 */

#include "connection_pool.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef HAVE_NGHTTP2
    #include <nghttp2/nghttp2.h>
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

    /* Close and free all connections */
    pooled_connection_t *conn = pool->connections;
    while (conn) {
        pooled_connection_t *next = conn->next;
        pool_connection_destroy(conn);
        conn = next;
    }

    /* Destroy mutex */
    if (pool->mutex) {
#ifdef _WIN32
        CRITICAL_SECTION *cs = (CRITICAL_SECTION*)pool->mutex;
        DeleteCriticalSection(cs);
        free(cs);
#else
        pthread_mutex_t *mutex = (pthread_mutex_t*)pool->mutex;
        pthread_mutex_destroy(mutex);
        free(mutex);
#endif
    }

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
                /* Remove from list and return */
                *curr = conn->next;
                pool->total_connections--;
                pool->active_connections++;

                /* Update last used time */
                conn->last_used = time(NULL);
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

    /* Ensure socket is in blocking mode before pooling */
    #ifdef _WIN32
        u_long mode = 0;  /* 0 = blocking */
        ioctlsocket(sockfd, FIONBIO, &mode);
    #else
        int flags = fcntl(sockfd, F_GETFL, 0);
        if (flags != -1) {
            fcntl(sockfd, F_SETFL, flags & ~O_NONBLOCK);
        }
    #endif

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
#endif

    return conn;
}

void pool_connection_destroy(pooled_connection_t *conn) {
    if (!conn) {
        return;
    }

#ifdef HAVE_NGHTTP2
    /* Destroy HTTP/2 session if present */
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
    }
    if (conn->target_host) {
        free(conn->target_host);
    }

    /* Free TLS info */
    if (conn->ja3_fingerprint) {
        free(conn->ja3_fingerprint);
    }
    if (conn->tls_version) {
        free(conn->tls_version);
    }
    if (conn->tls_cipher) {
        free(conn->tls_cipher);
    }

    /* Close SSL */
    if (conn->ssl) {
        /* Skip SSL_shutdown() as it can block indefinitely on stale/proxy connections.
         * SSL_free() will handle cleanup safely without blocking. */
        SSL_free(conn->ssl);
    }

    /* Close socket */
    if (conn->sockfd >= 0) {
        close(conn->sockfd);
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
