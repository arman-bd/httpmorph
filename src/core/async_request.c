/**
 * async_request.c - Async request state machine implementation
 */

#include "async_request.h"
#include "io_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* SSL/TLS support */
#include <openssl/ssl.h>
#include <openssl/err.h>

/* Platform-specific headers */
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <unistd.h>
    #include <errno.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netdb.h>
    #include <sys/time.h>
#endif

/* Default buffer sizes */
#define SEND_BUFFER_SIZE (64 * 1024)      /* 64KB */
#define RECV_BUFFER_SIZE (256 * 1024)     /* 256KB */

/* ID generation */
static uint64_t next_request_id = 1;

/**
 * Get current time in microseconds
 */
static uint64_t get_time_us(void) {
#ifdef _WIN32
    FILETIME ft;
    ULARGE_INTEGER uli;

    /* Get current time as FILETIME (100-nanosecond intervals since 1601-01-01) */
    GetSystemTimeAsFileTime(&ft);

    /* Convert to 64-bit integer */
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;

    /* Convert to microseconds since Unix epoch (1970-01-01) */
    /* FILETIME epoch is 1601-01-01, Unix epoch is 1970-01-01 */
    /* Difference is 11644473600 seconds */
    uint64_t microseconds = uli.QuadPart / 10; /* Convert 100-ns to microseconds */
    microseconds -= 11644473600ULL * 1000000ULL; /* Adjust to Unix epoch */

    return microseconds;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + (uint64_t)tv.tv_usec;
#endif
}

/**
 * Get state name for debugging
 */
const char* async_request_state_name(async_request_state_t state) {
    switch (state) {
        case ASYNC_STATE_INIT:              return "INIT";
        case ASYNC_STATE_DNS_LOOKUP:        return "DNS_LOOKUP";
        case ASYNC_STATE_CONNECTING:        return "CONNECTING";
        case ASYNC_STATE_TLS_HANDSHAKE:     return "TLS_HANDSHAKE";
        case ASYNC_STATE_SENDING_REQUEST:   return "SENDING_REQUEST";
        case ASYNC_STATE_RECEIVING_HEADERS: return "RECEIVING_HEADERS";
        case ASYNC_STATE_RECEIVING_BODY:    return "RECEIVING_BODY";
        case ASYNC_STATE_COMPLETE:          return "COMPLETE";
        case ASYNC_STATE_ERROR:             return "ERROR";
        default:                            return "UNKNOWN";
    }
}

/**
 * Create a new async request
 */
async_request_t* async_request_create(
    const httpmorph_request_t *request,
    io_engine_t *io_engine,
    SSL_CTX *ssl_ctx,
    uint32_t timeout_ms,
    async_request_callback_t callback,
    void *user_data)
{
    if (!request || !io_engine) {
        return NULL;
    }

    async_request_t *req = calloc(1, sizeof(async_request_t));
    if (!req) {
        return NULL;
    }

    /* Initialize basic fields */
    req->id = next_request_id++;
    req->state = ASYNC_STATE_INIT;
    req->request = (httpmorph_request_t*)request;  /* Store pointer */
    req->io_engine = io_engine;
    req->sockfd = -1;
    req->ssl = NULL;
    req->refcount = 1;
    req->dns_resolved = false;

    /* Determine if HTTPS first (needed before creating SSL) */
    req->is_https = request->use_tls;

    /* Timing */
    req->start_time_us = get_time_us();
    req->timeout_ms = timeout_ms;
    if (timeout_ms > 0) {
        req->deadline_us = req->start_time_us + (uint64_t)timeout_ms * 1000;
    } else {
        req->deadline_us = 0;  /* No timeout */
    }

    /* Allocate send buffer */
    req->send_buf = malloc(SEND_BUFFER_SIZE);
    if (!req->send_buf) {
        free(req);
        return NULL;
    }

    /* Allocate receive buffer */
    req->recv_buf = malloc(RECV_BUFFER_SIZE);
    if (!req->recv_buf) {
        free(req->send_buf);
        free(req);
        return NULL;
    }
    req->recv_capacity = RECV_BUFFER_SIZE;

    /* Set callback */
    req->on_complete = callback;
    req->user_data = user_data;

    /* Parse URL to extract hostname and port if not already set */
    if (!request->host && request->url) {
        char *scheme = NULL, *host = NULL, *path = NULL;
        uint16_t port = 0;

        extern int httpmorph_parse_url(const char *url, char **scheme, char **host, uint16_t *port, char **path);

        if (httpmorph_parse_url(request->url, &scheme, &host, &port, &path) == 0) {
            /* Store parsed values in request structure */
            ((httpmorph_request_t*)request)->host = host;  /* Transfer ownership */
            ((httpmorph_request_t*)request)->port = port;
            ((httpmorph_request_t*)request)->use_tls = (scheme && strcmp(scheme, "https") == 0);
            req->is_https = ((httpmorph_request_t*)request)->use_tls;

            /* Free scheme and path as we don't need them */
            free(scheme);
            free(path);
        }
    }

    /* Create SSL object for HTTPS requests */
    if (req->is_https && ssl_ctx) {
        req->ssl = SSL_new(ssl_ctx);
        if (!req->ssl) {
            /* Cleanup and return NULL */
            free(req->send_buf);
            free(req->recv_buf);
            free(req);
            return NULL;
        }

        /* Set SNI hostname if available */
        if (request->host) {
            SSL_set_tlsext_host_name(req->ssl, request->host);
        }

        /* Set SSL to non-blocking mode (will be done when socket is created) */
        printf("[async_request] Created SSL object for HTTPS (id=%lu)\n",
               (unsigned long)req->id);
    }

    return req;
}

/**
 * Destroy an async request
 */
void async_request_destroy(async_request_t *req) {
    if (!req) {
        return;
    }

    /* Clean up I/O operation */
    if (req->current_op) {
        io_op_destroy(req->current_op);
        req->current_op = NULL;
    }

    /* Close socket (but never close stdin/stdout/stderr) */
    if (req->sockfd > 2) {
        close(req->sockfd);
        req->sockfd = -1;
    }

    /* Clean up SSL */
    if (req->ssl) {
        SSL_free(req->ssl);
        req->ssl = NULL;
    }

    /* Free buffers */
    free(req->send_buf);
    free(req->recv_buf);

    /* Free response if allocated */
    if (req->response) {
        httpmorph_response_destroy(req->response);
        req->response = NULL;
    }

    free(req);
}

/**
 * Reference counting
 */
void async_request_ref(async_request_t *req) {
    if (req) {
        req->refcount++;
    }
}

void async_request_unref(async_request_t *req) {
    if (req) {
        req->refcount--;
        if (req->refcount <= 0) {
            async_request_destroy(req);
        }
    }
}

/**
 * Get current state
 */
async_request_state_t async_request_get_state(const async_request_t *req) {
    return req ? req->state : ASYNC_STATE_ERROR;
}

/**
 * Get file descriptor
 */
int async_request_get_fd(const async_request_t *req) {
    return req ? req->sockfd : -1;
}

/**
 * Check timeout
 */
bool async_request_is_timeout(const async_request_t *req) {
    if (!req || req->deadline_us == 0) {
        return false;
    }
    return get_time_us() >= req->deadline_us;
}

/**
 * Set error state
 */
void async_request_set_error(async_request_t *req, int error_code, const char *error_msg) {
    if (!req) {
        return;
    }

    req->state = ASYNC_STATE_ERROR;
    req->error_code = error_code;
    if (error_msg) {
        snprintf(req->error_msg, sizeof(req->error_msg), "%s", error_msg);
    }
}

/**
 * Get response
 */
httpmorph_response_t* async_request_get_response(async_request_t *req) {
    if (!req || req->state != ASYNC_STATE_COMPLETE) {
        return NULL;
    }
    return req->response;
}

/**
 * Get error message
 */
const char* async_request_get_error_message(const async_request_t *req) {
    if (!req || req->error_msg[0] == '\0') {
        return "No error message";
    }
    return req->error_msg;
}

/**
 * State: DNS lookup
 */
static int step_dns_lookup(async_request_t *req) {
    /* Check if already resolved */
    if (req->dns_resolved) {
        req->state = ASYNC_STATE_CONNECTING;
        return ASYNC_STATUS_IN_PROGRESS;
    }

    /* Perform blocking DNS lookup (for now) */
    /* Note: In production, this should use async DNS (getaddrinfo_a or thread pool) */
    const char *hostname = req->request->host;
    uint16_t port = req->request->port;

    if (!hostname) {
        async_request_set_error(req, -1, "No hostname specified");
        return ASYNC_STATUS_ERROR;
    }

    printf("[async_request] Resolving %s:%u (id=%lu)\n",
           hostname, port, (unsigned long)req->id);

    /* Setup hints for getaddrinfo */
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM; /* TCP socket */
    hints.ai_flags = AI_ADDRCONFIG;  /* Only return addresses we can use */

    /* Convert port to string */
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%u", port);

    /* Perform DNS lookup */
    struct addrinfo *result = NULL;
    int ret = getaddrinfo(hostname, port_str, &hints, &result);

    if (ret != 0) {
        char error_buf[256];
        snprintf(error_buf, sizeof(error_buf), "DNS lookup failed: %s", gai_strerror(ret));
        async_request_set_error(req, ret, error_buf);
        return ASYNC_STATUS_ERROR;
    }

    if (!result) {
        async_request_set_error(req, -1, "DNS lookup returned no results");
        return ASYNC_STATUS_ERROR;
    }

    /* Store the first result */
    memcpy(&req->addr, result->ai_addr, result->ai_addrlen);
    req->addr_len = result->ai_addrlen;
    req->dns_resolved = true;

    printf("[async_request] DNS resolved for %s:%u (id=%lu)\n",
           hostname, port, (unsigned long)req->id);

    /* Free the result */
    freeaddrinfo(result);

    /* Move to connecting state */
    req->state = ASYNC_STATE_CONNECTING;
    return ASYNC_STATUS_IN_PROGRESS;
}

/**
 * State: Connecting
 */
static int step_connecting(async_request_t *req) {
    /* Verify DNS was resolved */
    if (!req->dns_resolved) {
        async_request_set_error(req, -1, "DNS not resolved before connect");
        return ASYNC_STATUS_ERROR;
    }

    /* If socket not created yet, create it and initiate connection */
    if (req->sockfd < 0) {
        /* Get address family from resolved address */
        int af = ((struct sockaddr*)&req->addr)->sa_family;

        /* Create non-blocking socket */
        req->sockfd = io_socket_create_nonblocking(af, SOCK_STREAM, 0);
        if (req->sockfd < 0) {
            async_request_set_error(req, errno, "Failed to create socket");
            return ASYNC_STATUS_ERROR;
        }

        /* Set performance options */
        io_socket_set_performance_opts(req->sockfd);

        printf("[async_request] Connecting to %s:%u on fd=%d (id=%lu)\n",
               req->request->host, req->request->port, req->sockfd, (unsigned long)req->id);

        /* Attempt non-blocking connect */
        int ret = connect(req->sockfd, (struct sockaddr*)&req->addr, req->addr_len);

        if (ret == 0) {
            /* Connected immediately (rare for non-blocking) */
            printf("[async_request] Connected immediately (id=%lu)\n",
                   (unsigned long)req->id);

            /* Move to next state */
            if (req->is_https) {
                req->state = ASYNC_STATE_TLS_HANDSHAKE;
            } else {
                req->state = ASYNC_STATE_SENDING_REQUEST;
            }
            return ASYNC_STATUS_IN_PROGRESS;
        }

#ifndef _WIN32
        if (errno == EINPROGRESS) {
            /* Connection in progress, wait for writable */
            return ASYNC_STATUS_NEED_WRITE;
        }
#else
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
            /* Connection in progress, wait for writable */
            return ASYNC_STATUS_NEED_WRITE;
        }
#endif

        /* Connect failed immediately */
        async_request_set_error(req, errno, "Connection failed");
        return ASYNC_STATUS_ERROR;
    }

    /* Socket already exists, check if connect completed */
#ifndef _WIN32
    int error = 0;
    socklen_t len = sizeof(error);
    if (getsockopt(req->sockfd, SOL_SOCKET, SO_ERROR, &error, &len) == 0) {
        if (error == 0) {
            /* Connection successful */
            printf("[async_request] Connected successfully (id=%lu)\n",
                   (unsigned long)req->id);

            /* Move to next state */
            if (req->is_https) {
                req->state = ASYNC_STATE_TLS_HANDSHAKE;
            } else {
                req->state = ASYNC_STATE_SENDING_REQUEST;
                /* For plain HTTP, wait for socket to be writable before sending */
                return ASYNC_STATUS_NEED_WRITE;
            }
            return ASYNC_STATUS_IN_PROGRESS;
        } else if (error == EINPROGRESS || error == EALREADY) {
            /* Still connecting, need to wait */
            return ASYNC_STATUS_NEED_WRITE;
        } else {
            /* Connection failed */
            char error_buf[256];
            snprintf(error_buf, sizeof(error_buf), "Connection failed: %s", strerror(error));
            async_request_set_error(req, error, error_buf);
            return ASYNC_STATUS_ERROR;
        }
    }
#endif

    /* Waiting for connection to complete */
    return ASYNC_STATUS_NEED_WRITE;
}

/**
 * State: TLS handshake
 */
static int step_tls_handshake(async_request_t *req) {
    /* SSL object should exist for HTTPS */
    if (!req->ssl) {
        async_request_set_error(req, -1, "No SSL object for HTTPS");
        return ASYNC_STATUS_ERROR;
    }

    /* Bind SSL to socket if not already done */
    if (SSL_get_fd(req->ssl) != req->sockfd) {
        if (SSL_set_fd(req->ssl, req->sockfd) != 1) {
            async_request_set_error(req, -1, "Failed to bind SSL to socket");
            return ASYNC_STATUS_ERROR;
        }
        /* Set connect state (client mode) */
        SSL_set_connect_state(req->ssl);
        printf("[async_request] SSL bound to socket fd=%d (id=%lu)\n",
               req->sockfd, (unsigned long)req->id);
    }

    /* Perform non-blocking handshake */
    int ret = SSL_do_handshake(req->ssl);

    if (ret == 1) {
        /* Handshake complete */
        printf("[async_request] TLS handshake complete (id=%lu)\n",
               (unsigned long)req->id);
        req->state = ASYNC_STATE_SENDING_REQUEST;
        return ASYNC_STATUS_IN_PROGRESS;
    }

    /* Check error */
    int err = SSL_get_error(req->ssl, ret);
    switch (err) {
        case SSL_ERROR_WANT_READ:
            /* Need to wait for socket to be readable */
            return ASYNC_STATUS_NEED_READ;

        case SSL_ERROR_WANT_WRITE:
            /* Need to wait for socket to be writable */
            return ASYNC_STATUS_NEED_WRITE;

        case SSL_ERROR_ZERO_RETURN:
            /* Connection closed */
            async_request_set_error(req, -1, "TLS connection closed");
            return ASYNC_STATUS_ERROR;

        default:
            /* Other error */
            {
                char err_buf[256];
                ERR_error_string_n(ERR_get_error(), err_buf, sizeof(err_buf));
                snprintf(req->error_msg, sizeof(req->error_msg), "TLS handshake failed: %s", err_buf);
                req->state = ASYNC_STATE_ERROR;
            }
            return ASYNC_STATUS_ERROR;
    }
}

/**
 * Build HTTP request from httpmorph_request_t structure
 */
static int build_http_request(async_request_t *req) {
    httpmorph_request_t *request = req->request;

    /* Method string mapping */
    const char *method_str;
    switch (request->method) {
        case HTTPMORPH_GET:     method_str = "GET"; break;
        case HTTPMORPH_POST:    method_str = "POST"; break;
        case HTTPMORPH_PUT:     method_str = "PUT"; break;
        case HTTPMORPH_DELETE:  method_str = "DELETE"; break;
        case HTTPMORPH_HEAD:    method_str = "HEAD"; break;
        case HTTPMORPH_OPTIONS: method_str = "OPTIONS"; break;
        case HTTPMORPH_PATCH:   method_str = "PATCH"; break;
        default:               method_str = "GET"; break;
    }

    /* Extract path from URL (simple extraction) */
    const char *path = strchr(request->url, '/');
    if (path && path[0] == '/' && path[1] == '/') {
        /* Skip scheme (http:// or https://) */
        path = strchr(path + 2, '/');
    }
    if (!path || path[0] != '/') {
        path = "/";
    }

    /* Build request line: METHOD path HTTP/1.1\r\n */
    char *buf = (char *)req->send_buf;
    int written = snprintf(buf, SEND_BUFFER_SIZE,
                          "%s %s HTTP/1.1\r\n", method_str, path);
    if (written < 0 || written >= (int)SEND_BUFFER_SIZE) {
        return -1;
    }

    /* Add Host header */
    written += snprintf(buf + written, SEND_BUFFER_SIZE - written,
                       "Host: %s\r\n", request->host ? request->host : "localhost");
    if (written >= (int)SEND_BUFFER_SIZE) {
        return -1;
    }

    /* Add custom headers */
    for (size_t i = 0; i < request->header_count; i++) {
        written += snprintf(buf + written, SEND_BUFFER_SIZE - written,
                           "%s: %s\r\n",
                           request->headers[i].key,
                           request->headers[i].value);
        if (written >= (int)SEND_BUFFER_SIZE) {
            return -1;
        }
    }

    /* Add Content-Length if body present */
    if (request->body && request->body_len > 0) {
        written += snprintf(buf + written, SEND_BUFFER_SIZE - written,
                           "Content-Length: %zu\r\n", request->body_len);
        if (written >= (int)SEND_BUFFER_SIZE) {
            return -1;
        }
    }

    /* End of headers */
    written += snprintf(buf + written, SEND_BUFFER_SIZE - written, "\r\n");
    if (written >= (int)SEND_BUFFER_SIZE) {
        return -1;
    }

    /* Add body if present */
    if (request->body && request->body_len > 0) {
        if (written + request->body_len >= SEND_BUFFER_SIZE) {
            return -1;  /* Body too large */
        }
        memcpy(buf + written, request->body, request->body_len);
        written += request->body_len;
    }

    req->send_len = written;
    req->send_pos = 0;

    return 0;
}

/**
 * State: Sending request
 */
static int step_sending_request(async_request_t *req) {
    /* Build request if not already done */
    if (req->send_len == 0) {
        printf("[async_request] Building HTTP request (id=%lu)\n",
               (unsigned long)req->id);

        if (build_http_request(req) < 0) {
            async_request_set_error(req, HTTPMORPH_ERROR_MEMORY,
                                   "Failed to build HTTP request");
            req->state = ASYNC_STATE_ERROR;
            return ASYNC_STATUS_ERROR;
        }
    }

    /* Send data */
    while (req->send_pos < req->send_len) {
        ssize_t sent;

        if (req->ssl) {
            /* SSL send */
            sent = SSL_write(req->ssl,
                           req->send_buf + req->send_pos,
                           (int)(req->send_len - req->send_pos));

            if (sent <= 0) {
                int err = SSL_get_error(req->ssl, (int)sent);
                if (err == SSL_ERROR_WANT_WRITE) {
                    return ASYNC_STATUS_NEED_WRITE;
                } else if (err == SSL_ERROR_WANT_READ) {
                    return ASYNC_STATUS_NEED_READ;
                } else {
                    async_request_set_error(req, err, "SSL write failed");
                    return ASYNC_STATUS_ERROR;
                }
            }
        } else {
            /* Plain TCP send */
            sent = send(req->sockfd,
                       req->send_buf + req->send_pos,
                       req->send_len - req->send_pos,
                       0);

            if (sent < 0) {
#ifdef _WIN32
                int err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK) {
                    return ASYNC_STATUS_NEED_WRITE;
                }
#else
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return ASYNC_STATUS_NEED_WRITE;
                }
#endif
                async_request_set_error(req, errno, "Send failed");
                return ASYNC_STATUS_ERROR;
            }

            if (sent == 0) {
                async_request_set_error(req, -1, "Connection closed");
                return ASYNC_STATUS_ERROR;
            }
        }

        req->send_pos += sent;
    }

    /* All data sent */
    printf("[async_request] Request sent (%zu bytes) (id=%lu)\n",
           req->send_len, (unsigned long)req->id);

    req->state = ASYNC_STATE_RECEIVING_HEADERS;
    return ASYNC_STATUS_IN_PROGRESS;
}

/**
 * State: Receiving headers
 */
static int step_receiving_headers(async_request_t *req) {
    /* Receive data */
    ssize_t received;

    if (req->ssl) {
        /* SSL receive */
        received = SSL_read(req->ssl,
                          req->recv_buf + req->recv_len,
                          (int)(req->recv_capacity - req->recv_len));

        if (received <= 0) {
            int err = SSL_get_error(req->ssl, (int)received);
            if (err == SSL_ERROR_WANT_READ) {
                return ASYNC_STATUS_NEED_READ;
            } else if (err == SSL_ERROR_WANT_WRITE) {
                return ASYNC_STATUS_NEED_WRITE;
            } else if (err == SSL_ERROR_ZERO_RETURN) {
                async_request_set_error(req, -1, "Connection closed");
                return ASYNC_STATUS_ERROR;
            } else {
                async_request_set_error(req, err, "SSL read failed");
                return ASYNC_STATUS_ERROR;
            }
        }
    } else {
        /* Plain TCP receive */
        received = recv(req->sockfd,
                       req->recv_buf + req->recv_len,
                       req->recv_capacity - req->recv_len,
                       0);

        if (received < 0) {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                return ASYNC_STATUS_NEED_READ;
            }
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return ASYNC_STATUS_NEED_READ;
            }
#endif
            async_request_set_error(req, errno, "Receive failed");
            return ASYNC_STATUS_ERROR;
        }

        if (received == 0) {
            async_request_set_error(req, -1, "Connection closed");
            return ASYNC_STATUS_ERROR;
        }
    }

    req->recv_len += received;

    /* Look for end of headers (\r\n\r\n) */
    if (req->recv_len >= 4) {
        for (size_t i = 0; i <= req->recv_len - 4; i++) {
            if (memcmp(req->recv_buf + i, "\r\n\r\n", 4) == 0) {
                /* Found end of headers */
                req->headers_complete = true;
                req->headers_end_pos = i + 4;

                printf("[async_request] Headers received (%zu bytes) (id=%lu)\n",
                       req->headers_end_pos, (unsigned long)req->id);

                /* Parse headers for Content-Length and Transfer-Encoding */
                req->content_length = 0;
                req->chunked_encoding = false;

                /* Search for Content-Length header (case-insensitive) */
                char *headers_start = (char *)req->recv_buf;
                char *headers_end = headers_start + req->headers_end_pos;

                /* Look for "Content-Length:" */
                for (char *p = headers_start; p < headers_end - 16; p++) {
                    if ((p[0] == 'C' || p[0] == 'c') &&
                        (p[1] == 'o' || p[1] == 'O') &&
                        (p[2] == 'n' || p[2] == 'N') &&
                        (p[3] == 't' || p[3] == 'T') &&
                        (p[4] == 'e' || p[4] == 'E') &&
                        (p[5] == 'n' || p[5] == 'N') &&
                        (p[6] == 't' || p[6] == 'T') &&
                        p[7] == '-' &&
                        (p[8] == 'L' || p[8] == 'l') &&
                        (p[9] == 'e' || p[9] == 'E') &&
                        (p[10] == 'n' || p[10] == 'N') &&
                        (p[11] == 'g' || p[11] == 'G') &&
                        (p[12] == 't' || p[12] == 'T') &&
                        (p[13] == 'h' || p[13] == 'H') &&
                        p[14] == ':') {
                        /* Found Content-Length header */
                        p += 15;
                        /* Skip whitespace */
                        while (p < headers_end && (*p == ' ' || *p == '\t')) p++;
                        /* Parse number */
                        req->content_length = 0;
                        while (p < headers_end && *p >= '0' && *p <= '9') {
                            req->content_length = req->content_length * 10 + (*p - '0');
                            p++;
                        }
                        break;
                    }
                }

                /* Look for "Transfer-Encoding: chunked" */
                for (char *p = headers_start; p < headers_end - 25; p++) {
                    if ((p[0] == 'T' || p[0] == 't') &&
                        (p[1] == 'r' || p[1] == 'R') &&
                        (p[2] == 'a' || p[2] == 'A') &&
                        (p[3] == 'n' || p[3] == 'N') &&
                        (p[4] == 's' || p[4] == 'S') &&
                        (p[5] == 'f' || p[5] == 'F') &&
                        (p[6] == 'e' || p[6] == 'E') &&
                        (p[7] == 'r' || p[7] == 'R') &&
                        p[8] == '-' &&
                        (p[9] == 'E' || p[9] == 'e') &&
                        (p[10] == 'n' || p[10] == 'N') &&
                        (p[11] == 'c' || p[11] == 'C') &&
                        (p[12] == 'o' || p[12] == 'O') &&
                        (p[13] == 'd' || p[13] == 'D') &&
                        (p[14] == 'i' || p[14] == 'I') &&
                        (p[15] == 'n' || p[15] == 'N') &&
                        (p[16] == 'g' || p[16] == 'G') &&
                        p[17] == ':') {
                        /* Found Transfer-Encoding header, check if chunked */
                        p += 18;
                        /* Skip whitespace */
                        while (p < headers_end && (*p == ' ' || *p == '\t')) p++;
                        /* Check for "chunked" */
                        if (p < headers_end - 7 &&
                            (p[0] == 'c' || p[0] == 'C') &&
                            (p[1] == 'h' || p[1] == 'H') &&
                            (p[2] == 'u' || p[2] == 'U') &&
                            (p[3] == 'n' || p[3] == 'N') &&
                            (p[4] == 'k' || p[4] == 'K') &&
                            (p[5] == 'e' || p[5] == 'E') &&
                            (p[6] == 'd' || p[6] == 'D')) {
                            req->chunked_encoding = true;
                        }
                        break;
                    }
                }

                printf("[async_request] Content-Length: %zu, Chunked: %d (id=%lu)\n",
                       req->content_length, req->chunked_encoding, (unsigned long)req->id);

                /* Check if we already have body data in the buffer */
                size_t body_start = req->headers_end_pos;
                if (body_start < req->recv_len) {
                    /* We have some body data already */
                    req->body_received = req->recv_len - body_start;
                    printf("[async_request] Already received %zu bytes of body with headers (id=%lu)\n",
                           req->body_received, (unsigned long)req->id);
                }

                req->state = ASYNC_STATE_RECEIVING_BODY;
                return ASYNC_STATUS_IN_PROGRESS;
            }
        }
    }

    /* Need more data */
    return ASYNC_STATUS_NEED_READ;
}

/**
 * State: Receiving body
 */
static int step_receiving_body(async_request_t *req) {
    /* If no body expected, complete immediately */
    if (req->content_length == 0 && !req->chunked_encoding) {
        printf("[async_request] No body to receive (id=%lu)\n",
               (unsigned long)req->id);

        /* Create response object for empty body */
        if (!req->response) {
            req->response = calloc(1, sizeof(httpmorph_response_t));
            if (req->response) {
                req->response->body = NULL;
                req->response->body_len = 0;
                req->response->status_code = 200;  /* TODO: Parse from headers */
                req->response->http_version = HTTPMORPH_VERSION_1_1;
                req->response->error = HTTPMORPH_OK;
            }
        }

        req->state = ASYNC_STATE_COMPLETE;
        return ASYNC_STATUS_COMPLETE;
    }

    /* Check if we already have all the body data */
    if (!req->chunked_encoding && req->body_received >= req->content_length) {
        printf("[async_request] Body already complete (%zu bytes) (id=%lu)\n",
               req->body_received, (unsigned long)req->id);

        /* Create response object */
        if (!req->response) {
            req->response = calloc(1, sizeof(httpmorph_response_t));
            if (req->response) {
                /* Extract body from recv_buf (starts after headers) */
                size_t body_start = req->headers_end_pos;
                if (req->content_length > 0) {
                    req->response->body = malloc(req->content_length);
                    if (req->response->body) {
                        memcpy(req->response->body, req->recv_buf + body_start, req->content_length);
                        req->response->body_len = req->content_length;
                    }
                }
                req->response->status_code = 200;  // TODO: Parse from headers
                req->response->http_version = HTTPMORPH_VERSION_1_1;
                req->response->error = HTTPMORPH_OK;
            }
        }

        req->state = ASYNC_STATE_COMPLETE;
        return ASYNC_STATUS_COMPLETE;
    }

    /* Receive body data */
    ssize_t received;

    if (req->ssl) {
        received = SSL_read(req->ssl,
                          req->recv_buf + req->recv_len,
                          (int)(req->recv_capacity - req->recv_len));

        if (received <= 0) {
            int err = SSL_get_error(req->ssl, (int)received);
            if (err == SSL_ERROR_WANT_READ) {
                return ASYNC_STATUS_NEED_READ;
            } else if (err == SSL_ERROR_WANT_WRITE) {
                return ASYNC_STATUS_NEED_WRITE;
            } else if (err == SSL_ERROR_ZERO_RETURN) {
                /* Connection closed - check if we got all data */
                if (req->content_length > 0 &&
                    req->body_received < req->content_length) {
                    async_request_set_error(req, -1, "Incomplete body");
                    return ASYNC_STATUS_ERROR;
                }
                req->state = ASYNC_STATE_COMPLETE;
                return ASYNC_STATUS_COMPLETE;
            } else {
                async_request_set_error(req, err, "SSL read failed");
                return ASYNC_STATUS_ERROR;
            }
        }
    } else {
        received = recv(req->sockfd,
                       req->recv_buf + req->recv_len,
                       req->recv_capacity - req->recv_len,
                       0);

        if (received < 0) {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                return ASYNC_STATUS_NEED_READ;
            }
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return ASYNC_STATUS_NEED_READ;
            }
#endif
            async_request_set_error(req, errno, "Receive failed");
            return ASYNC_STATUS_ERROR;
        }

        if (received == 0) {
            /* Connection closed - check if we got all data */
            if (req->content_length > 0 &&
                req->body_received < req->content_length) {
                async_request_set_error(req, -1, "Incomplete body");
                return ASYNC_STATUS_ERROR;
            }
            req->state = ASYNC_STATE_COMPLETE;
            return ASYNC_STATUS_COMPLETE;
        }
    }

    req->recv_len += received;
    req->body_received += received;

    /* Check if we received all data */
    if (req->content_length > 0 && req->body_received >= req->content_length) {
        printf("[async_request] Body received (%zu bytes) (id=%lu)\n",
               req->body_received, (unsigned long)req->id);

        /* Create response object */
        if (!req->response) {
            req->response = calloc(1, sizeof(httpmorph_response_t));
            if (req->response) {
                /* Extract body from recv_buf (starts after headers) */
                size_t body_start = req->headers_end_pos;
                if (req->content_length > 0) {
                    req->response->body = malloc(req->content_length);
                    if (req->response->body) {
                        memcpy(req->response->body, req->recv_buf + body_start, req->content_length);
                        req->response->body_len = req->content_length;
                    }
                }
                req->response->status_code = 200;  // TODO: Parse from headers
                req->response->http_version = HTTPMORPH_VERSION_1_1;
                req->response->error = HTTPMORPH_OK;
            }
        }

        req->state = ASYNC_STATE_COMPLETE;
        return ASYNC_STATUS_COMPLETE;
    }

    /* Need more data */
    return ASYNC_STATUS_NEED_READ;
}

/**
 * Step the async request state machine
 */
int async_request_step(async_request_t *req) {
    if (!req) {
        return ASYNC_STATUS_ERROR;
    }

    /* Check timeout */
    if (async_request_is_timeout(req)) {
        async_request_set_error(req, -1, "Request timeout");
        if (req->on_complete) {
            req->on_complete(req, ASYNC_STATUS_ERROR);
        }
        return ASYNC_STATUS_ERROR;
    }

    /* State machine */
    switch (req->state) {
        case ASYNC_STATE_INIT:
            /* Start with DNS lookup */
            req->state = ASYNC_STATE_DNS_LOOKUP;
            return ASYNC_STATUS_IN_PROGRESS;

        case ASYNC_STATE_DNS_LOOKUP:
            return step_dns_lookup(req);

        case ASYNC_STATE_CONNECTING:
            return step_connecting(req);

        case ASYNC_STATE_TLS_HANDSHAKE:
            return step_tls_handshake(req);

        case ASYNC_STATE_SENDING_REQUEST:
            return step_sending_request(req);

        case ASYNC_STATE_RECEIVING_HEADERS:
            return step_receiving_headers(req);

        case ASYNC_STATE_RECEIVING_BODY:
            return step_receiving_body(req);

        case ASYNC_STATE_COMPLETE:
            /* Already complete */
            if (req->on_complete) {
                req->on_complete(req, ASYNC_STATUS_COMPLETE);
            }
            return ASYNC_STATUS_COMPLETE;

        case ASYNC_STATE_ERROR:
            /* Already in error state */
            if (req->on_complete) {
                req->on_complete(req, ASYNC_STATUS_ERROR);
            }
            return ASYNC_STATUS_ERROR;

        default:
            async_request_set_error(req, -1, "Invalid state");
            return ASYNC_STATUS_ERROR;
    }
}
