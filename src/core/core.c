/**
 * core.c - Core orchestration for HTTP requests
 */

#include "internal/core.h"
#include "internal/util.h"
#include "internal/url.h"
#include "internal/network.h"
#include "internal/proxy.h"
#include "internal/tls.h"
#include "internal/compression.h"
#include "internal/http1.h"
#include "internal/http2_logic.h"
#include "internal/response.h"
#include "connection_pool.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #define close closesocket
#else
    #include <unistd.h>
    #include <fcntl.h>
#endif

/**
 * Execute an HTTP request (main orchestration function)
 */
httpmorph_response_t* httpmorph_request_execute(
    httpmorph_client_t *client,
    const httpmorph_request_t *request,
    httpmorph_pool_t *pool) {

    if (!client || !request || !request->url) {
        return NULL;
    }

    httpmorph_response_t *response = httpmorph_response_create(client->buffer_pool);
    if (!response) {
        return NULL;
    }

    uint64_t start_time = httpmorph_get_time_us();
    int sockfd = -1;
    SSL *ssl = NULL;
    char *proxy_user = NULL;
    char *proxy_pass = NULL;
    pooled_connection_t *pooled_conn = NULL;  /* Track if we got connection from pool */
    bool use_http2 = false;  /* Track if HTTP/2 is being used */

    /* Parse URL */
    char *scheme = NULL, *host = NULL, *path = NULL;
    uint16_t port = 0;

    if (httpmorph_parse_url(request->url, &scheme, &host, &port, &path) != 0) {
        response->error = HTTPMORPH_ERROR_PARSE;
        response->error_message = strdup("Failed to parse URL");
        goto cleanup;
    }

    bool use_tls = (strcmp(scheme, "https") == 0);

    /* 1. TCP Connection (direct or via proxy) */
    uint64_t connect_time = 0;

    if (request->proxy_url) {
        /* Connect via proxy - try pool first for connection reuse */
        char *proxy_host = NULL;
        uint16_t proxy_port = 0;
        bool proxy_use_tls = false;
        SSL *proxy_ssl = NULL;

        /* Don't use connection pool for proxy connections - they are not pooled
         * due to SSL_CTX use-after-free issues (see line 532 below).
         * If we retrieve a connection from pool here, it would be a stale direct
         * connection which cannot be used for proxy requests. */

        /* If no pooled connection, create new proxy connection */
        if (sockfd < 0) {
            /* Parse proxy URL */
            if (httpmorph_parse_proxy_url(request->proxy_url, &proxy_host, &proxy_port,
                               &proxy_user, &proxy_pass, &proxy_use_tls) != 0) {
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
            sockfd = httpmorph_tcp_connect(proxy_host, proxy_port, request->timeout_ms, &connect_time);
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

            /* If proxy uses TLS, establish TLS connection to proxy */
            if (proxy_use_tls) {
                uint64_t proxy_tls_time = 0;
                proxy_ssl = httpmorph_tls_connect(client->ssl_ctx, sockfd, proxy_host, client->browser_profile,
                                       false, request->verify_ssl, &proxy_tls_time);
                if (!proxy_ssl) {
                    if (sockfd > 2) close(sockfd);
                    sockfd = -1;
                    free(proxy_host);
                    free(proxy_user);
                    free(proxy_pass);
                    proxy_user = NULL;
                    proxy_pass = NULL;
                    response->error = HTTPMORPH_ERROR_TLS;
                    response->error_message = strdup("Failed to establish TLS with proxy");
                    goto cleanup;
                }
            }

            /* For HTTPS destinations, send CONNECT request to establish tunnel */
            if (use_tls) {
                if (httpmorph_proxy_connect(sockfd, proxy_ssl, host, port, proxy_user, proxy_pass,
                                request->timeout_ms) != 0) {
                    if (proxy_ssl) SSL_free(proxy_ssl);
                    if (sockfd > 2) close(sockfd);
                    sockfd = -1;
                    free(proxy_host);
                    free(proxy_user);
                    free(proxy_pass);
                    proxy_user = NULL;
                    proxy_pass = NULL;
                    response->error = HTTPMORPH_ERROR_NETWORK;
                    response->error_message = strdup("Proxy CONNECT failed");
                    goto cleanup;
                }
                /* After CONNECT succeeds, we have a tunnel - free proxy SSL as we'll establish new TLS to destination */
                if (proxy_ssl) {
                    SSL_free(proxy_ssl);
                    proxy_ssl = NULL;
                }
            }

            free(proxy_host);
        }
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

                /* Restore TLS info from pooled connection BEFORE potential destruction */
                if (sockfd >= 0) {
                    /* Connection reused - no connect/TLS time */
                    connect_time = 0;
                    response->tls_time_us = 0;

                    if (pooled_conn->ja3_fingerprint) {
                        response->ja3_fingerprint = strdup(pooled_conn->ja3_fingerprint);
                    }
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
            } else {
            }
        }

        /* If no pooled connection, create new one */
        if (sockfd < 0) {
            sockfd = httpmorph_tcp_connect(host, port, request->timeout_ms, &connect_time);
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
        ssl = httpmorph_tls_connect(client->ssl_ctx, sockfd, host, client->browser_profile,
                         request->http2_enabled, request->verify_ssl, &tls_time);
        if (!ssl) {
            response->error = HTTPMORPH_ERROR_TLS;
            response->error_message = strdup("TLS handshake failed");
            goto cleanup;
        }
        response->tls_time_us = tls_time;

        /* Calculate JA3 fingerprint (only for new connections) */
        response->ja3_fingerprint = httpmorph_calculate_ja3(ssl, client->browser_profile);

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
        uint64_t first_byte_time = httpmorph_get_time_us();
        int http2_result;

        /* Use pooled version for session reuse if connection came from pool */
        if (pooled_conn && pooled_conn->is_http2) {
            /* Use concurrent version if session manager exists (high-performance mode) */
            if (pooled_conn->http2_session_manager) {
                http2_result = httpmorph_http2_request_concurrent(pooled_conn, request, host, path, response);
            } else {
                /* Fall back to sequential pooled version */
                http2_result = httpmorph_http2_request_pooled(pooled_conn, request, host, path, response);
            }
        } else {
            http2_result = httpmorph_http2_request(ssl, request, host, path, response);
        }

        if (http2_result != 0) {
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
    if (httpmorph_send_http_request(ssl, sockfd, request, host, path, scheme, port, using_proxy, proxy_user, proxy_pass) != 0) {
        /* If send failed on a pooled connection, retry with fresh connection */
        if (pooled_conn) {
            /* Destroy the stale pooled connection */
            pool_connection_destroy(pooled_conn);
            pooled_conn = NULL;
            sockfd = -1;
            ssl = NULL;

            /* Create new connection */
            sockfd = httpmorph_tcp_connect(host, port, request->timeout_ms, &connect_time);
            if (sockfd < 0) {
                response->error = HTTPMORPH_ERROR_NETWORK;
                response->error_message = strdup("Failed to connect after retry");
                goto cleanup;
            }
            response->connect_time_us = connect_time;

            /* New TLS handshake if needed */
            if (use_tls) {
                uint64_t tls_time = 0;
                ssl = httpmorph_tls_connect(client->ssl_ctx, sockfd, host, client->browser_profile,
                                request->http2_enabled, request->verify_ssl, &tls_time);
                if (!ssl) {
                    response->error = HTTPMORPH_ERROR_TLS;
                    response->error_message = strdup("TLS handshake failed on retry");
                    goto cleanup;
                }
                response->tls_time_us = tls_time;
            }

            /* Retry sending request with fresh connection */
            if (httpmorph_send_http_request(ssl, sockfd, request, host, path, scheme, port, using_proxy, proxy_user, proxy_pass) != 0) {
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
    int recv_result = httpmorph_recv_http_response(ssl, sockfd, response, &first_byte_time, &connection_will_close, request->method);

    /* If pooled connection failed, retry with new connection */
    if (recv_result != 0 && pooled_conn) {

        /* Destroy the failed pooled connection */
        pool_connection_destroy(pooled_conn);
        pooled_conn = NULL;
        sockfd = -1;
        ssl = NULL;

        /* Create new connection */
        sockfd = httpmorph_tcp_connect(host, port, request->timeout_ms, &connect_time);
        if (sockfd < 0) {
            response->error = HTTPMORPH_ERROR_NETWORK;
            response->error_message = strdup("Failed to connect");
            goto cleanup;
        }
        response->connect_time_us = connect_time;

        /* New TLS handshake */
        if (use_tls) {
            uint64_t tls_time = 0;
            ssl = httpmorph_tls_connect(client->ssl_ctx, sockfd, host, client->browser_profile,
                             request->http2_enabled, request->verify_ssl, &tls_time);
            if (!ssl) {
                response->error = HTTPMORPH_ERROR_TLS;
                response->error_message = strdup("TLS handshake failed");
                goto cleanup;
            }
            response->tls_time_us = tls_time;
        }

        /* Reset response completely for retry */
        for (size_t i = 0; i < response->header_count; i++) {
            free(response->headers[i].key);
            free(response->headers[i].value);
        }
        response->header_count = 0;
        response->body_len = 0;
        response->status_code = 0;

        /* Resend HTTP request */
        if (httpmorph_send_http_request(ssl, sockfd, request, host, path, scheme, port, using_proxy, proxy_user, proxy_pass) != 0) {
            response->error = HTTPMORPH_ERROR_NETWORK;
            response->error_message = strdup("Failed to send request");
            goto cleanup;
        }

        /* Retry receiving response */
        recv_result = httpmorph_recv_http_response(ssl, sockfd, response, &first_byte_time, &connection_will_close, request->method);
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
        httpmorph_decompress_gzip(response);
    }

        /* 6. Check if total time exceeded timeout (only if no error yet) */
        if (response->error == HTTPMORPH_OK || response->error == 0) {
            uint64_t elapsed_us = httpmorph_get_time_us() - start_time;
            uint64_t timeout_us = (uint64_t)request->timeout_ms * 1000;
            if (elapsed_us > timeout_us) {
                response->error = HTTPMORPH_ERROR_TIMEOUT;
                response->error_message = strdup("Request timed out");
            } else {
                response->error = HTTPMORPH_OK;
            }
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
                /* Clear local references to prevent double-free - pool_connection_destroy already freed them */
                sockfd = -1;
                ssl = NULL;
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
            /* Don't pool proxy connections - they can cause SSL_CTX use-after-free issues */
            if (use_http2 || request->proxy_url) {
                conn_to_pool = NULL;
            } else {
                conn_to_pool = pool_connection_create(host, port, sockfd, ssl, use_http2);
                /* Store TLS info in pooled connection for future reuse with error checking */
                if (conn_to_pool && ssl) {
                    bool alloc_failed = false;

                    if (response->ja3_fingerprint) {
                        conn_to_pool->ja3_fingerprint = strdup(response->ja3_fingerprint);
                        if (!conn_to_pool->ja3_fingerprint) alloc_failed = true;
                    }
                    if (response->tls_version && !alloc_failed) {
                        conn_to_pool->tls_version = strdup(response->tls_version);
                        if (!conn_to_pool->tls_version) alloc_failed = true;
                    }
                    if (response->tls_cipher && !alloc_failed) {
                        conn_to_pool->tls_cipher = strdup(response->tls_cipher);
                        if (!conn_to_pool->tls_cipher) alloc_failed = true;
                    }

                    /* If any allocation failed, destroy connection instead of pooling */
                    if (alloc_failed) {
                        pool_connection_destroy(conn_to_pool);
                        conn_to_pool = NULL;
                    }
                }
                /* Store proxy info for proxy connections */
                if (conn_to_pool && request->proxy_url) {
                    conn_to_pool->is_proxy = true;
                    /* Free old values if already set (connection reuse) */
                    if (conn_to_pool->proxy_url) {
                        free(conn_to_pool->proxy_url);
                    }
                    if (conn_to_pool->target_host) {
                        free(conn_to_pool->target_host);
                    }
                    conn_to_pool->proxy_url = strdup(request->proxy_url);
                    conn_to_pool->target_host = strdup(host);
                    conn_to_pool->target_port = port;
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
    /* Close socket but never close stdin/stdout/stderr */
    if (sockfd > 2) {
        close(sockfd);
    }

    free(scheme);
    free(host);
    free(path);
    free(proxy_user);
    free(proxy_pass);

    /* Automatically decompress response body if compressed */
    if (response->error == HTTPMORPH_OK) {
        httpmorph_auto_decompress(response);
    }

    response->total_time_us = httpmorph_get_time_us() - start_time;

    return response;
}
