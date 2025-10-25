/**
 * connection_pool.c - HTTP connection pooling implementation
 *
 * Implements connection reuse for HTTP keep-alive.
 */

#include "connection_pool.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Platform-specific includes */
#ifdef _WIN32
    #include <winsock2.h>
    #define close closesocket
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <fcntl.h>
    #include <errno.h>
#endif

#include <openssl/ssl.h>

/* === Pool Management === */

httpmorph_pool_t* pool_create(void) {
    httpmorph_pool_t *pool = calloc(1, sizeof(httpmorph_pool_t));
    if (!pool) {
        return NULL;
    }

    pool->connections = NULL;
    pool->total_connections = 0;
    pool->active_connections = 0;
    pool->max_connections_per_host = POOL_MAX_CONNECTIONS_PER_HOST;
    pool->max_total_connections = POOL_MAX_TOTAL_CONNECTIONS;
    pool->idle_timeout_seconds = POOL_IDLE_TIMEOUT_SECONDS;

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

    free(pool);
}

void pool_cleanup_idle(httpmorph_pool_t *pool) {
    if (!pool) {
        return;
    }

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
}

/* === Connection Operations === */

pooled_connection_t* pool_get_connection(httpmorph_pool_t *pool,
                                        const char *host,
                                        int port) {
    if (!pool || !host) {
        return NULL;
    }

    /* Build host key */
    char host_key[POOL_MAX_HOST_KEY_LEN];
    pool_build_host_key(host, port, host_key);

    /* Search for matching connection */
    pooled_connection_t **curr = &pool->connections;
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

                return conn;
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

    /* No connection found */
    return NULL;
}

bool pool_put_connection(httpmorph_pool_t *pool, pooled_connection_t *conn) {
    if (!pool || !conn) {
        return false;
    }

    /* Validate connection before pooling */
    if (!pool_connection_validate(conn)) {
        pool_connection_destroy(conn);
        return false;
    }

    /* Check if pool is full */
    if (pool->total_connections >= pool->max_total_connections) {
        /* Pool is full - close connection */
        pool_connection_destroy(conn);
        return false;
    }

    /* Check per-host limit */
    int host_conn_count = pool_count_connections_for_host(pool, conn->host_key);
    if (host_conn_count >= pool->max_connections_per_host) {
        /* Too many connections for this host */
        pool_connection_destroy(conn);
        return false;
    }

    /* Add to pool */
    conn->last_used = time(NULL);
    conn->next = pool->connections;
    pool->connections = conn;
    pool->total_connections++;

    if (pool->active_connections > 0) {
        pool->active_connections--;
    }

    return true;
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

    /* Drain any pending SSL buffer data before pooling */
    if (ssl) {
        int pending = SSL_pending(ssl);
        if (pending > 0) {
            char drain_buf[4096];
            while (SSL_pending(ssl) > 0) {
                int n = SSL_read(ssl, drain_buf, sizeof(drain_buf));
                if (n <= 0) break;
            }
        }
    }

    pooled_connection_t *conn = calloc(1, sizeof(pooled_connection_t));
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
    conn->last_used = time(NULL);
    conn->next = NULL;

    return conn;
}

void pool_connection_destroy(pooled_connection_t *conn) {
    if (!conn) {
        return;
    }

    /* Close SSL */
    if (conn->ssl) {
        SSL_shutdown(conn->ssl);
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

    /* Check SSL shutdown state if this is an SSL connection */
    if (conn->ssl) {
        int shutdown_state = SSL_get_shutdown(conn->ssl);
        if (shutdown_state != 0) {
            return false;
        }

        /* For SSL connections, use SSL_peek() to check if SSL session is alive */
        /* Set socket to non-blocking temporarily */
        #ifdef _WIN32
            u_long mode = 1;
            ioctlsocket(conn->sockfd, FIONBIO, &mode);
        #else
            int flags = fcntl(conn->sockfd, F_GETFL, 0);
            if (flags == -1) {
                return false;
            }
            if (fcntl(conn->sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
                return false;
            }
        #endif

        /* Try to peek at SSL data */
        char buf[1];
        int result = SSL_peek(conn->ssl, buf, 1);

        /* Restore blocking mode */
        #ifdef _WIN32
            mode = 0;
            ioctlsocket(conn->sockfd, FIONBIO, &mode);
        #else
            fcntl(conn->sockfd, F_SETFL, flags & ~O_NONBLOCK);
        #endif

        if (result <= 0) {
            int ssl_error = SSL_get_error(conn->ssl, result);
            /* SSL_ERROR_WANT_READ (2) means no data available, which is OK */
            if (ssl_error != SSL_ERROR_WANT_READ && ssl_error != SSL_ERROR_WANT_WRITE) {
                /* SSL session is closed or has error */
                return false;
            }
        }
    } else {
        /* For non-SSL connections, use regular MSG_PEEK */
        char buf[1];
        #ifdef _WIN32
            /* Set to non-blocking */
            u_long mode = 1;
            ioctlsocket(conn->sockfd, FIONBIO, &mode);

            int result = recv(conn->sockfd, buf, 1, MSG_PEEK);

            /* Restore to blocking */
            mode = 0;
            ioctlsocket(conn->sockfd, FIONBIO, &mode);

            if (result == SOCKET_ERROR) {
                int error = WSAGetLastError();
                if (error != WSAEWOULDBLOCK) {
                    return false;  /* Socket is dead */
                }
            }
        #else
            /* Set socket to non-blocking for peek */
            int flags = fcntl(conn->sockfd, F_GETFL, 0);
            if (flags == -1) {
                return false;
            }

            if (fcntl(conn->sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
                return false;
            }

            int result = recv(conn->sockfd, buf, 1, MSG_PEEK);
            int saved_errno = errno;

            /* Always restore to blocking mode explicitly */
            fcntl(conn->sockfd, F_SETFL, flags & ~O_NONBLOCK);

            if (result == 0) {
                /* Connection closed by peer */
                return false;
            } else if (result < 0) {
                /* Check errno */
                if (saved_errno != EAGAIN && saved_errno != EWOULDBLOCK) {
                    /* Real error - connection is dead */
                    return false;
                }
                /* EAGAIN/EWOULDBLOCK means no data available, which is fine */
            }
        #endif
    }

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
