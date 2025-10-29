/**
 * async_request.c - Async request state machine implementation
 */

#include "async_request.h"
#include "io_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

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
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + (uint64_t)tv.tv_usec;
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

    /* Determine if HTTPS */
    req->is_https = false;  /* TODO: Parse from request URL */

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
        /* TODO: Free response structure */
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
 * State: DNS lookup
 */
static int step_dns_lookup(async_request_t *req) {
    /* TODO: Implement async DNS resolution */
    /* For now, this is a placeholder that moves to next state */

    printf("[async_request] DNS lookup not implemented yet (id=%lu)\n",
           (unsigned long)req->id);

    /* Move to connecting state */
    req->state = ASYNC_STATE_CONNECTING;
    return ASYNC_STATUS_IN_PROGRESS;
}

/**
 * State: Connecting
 */
static int step_connecting(async_request_t *req) {
    /* If socket not created yet, create it */
    if (req->sockfd < 0) {
        /* Create non-blocking socket */
        req->sockfd = io_socket_create_nonblocking(AF_INET, SOCK_STREAM, 0);
        if (req->sockfd < 0) {
            async_request_set_error(req, errno, "Failed to create socket");
            return ASYNC_STATUS_ERROR;
        }

        /* Set performance options */
        io_socket_set_performance_opts(req->sockfd);

        /* TODO: Get address from DNS lookup */
        /* For now, this is incomplete - need DNS resolution first */
        printf("[async_request] Connecting on fd=%d (id=%lu)\n",
               req->sockfd, (unsigned long)req->id);
    }

    /* Attempt non-blocking connect */
    /* Note: This is a simplified version - real implementation needs */
    /* actual address from DNS lookup */

#ifndef _WIN32
    /* Check if connect completed (for subsequent calls) */
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
            }
            return ASYNC_STATUS_IN_PROGRESS;
        } else if (error == EINPROGRESS || error == EALREADY) {
            /* Still connecting, need to wait */
            return ASYNC_STATUS_NEED_WRITE;
        } else {
            /* Connection failed */
            async_request_set_error(req, error, "Connection failed");
            return ASYNC_STATUS_ERROR;
        }
    }
#endif

    /* First connect attempt or waiting for completion */
    /* Register for write event (connect completes when writable) */
    return ASYNC_STATUS_NEED_WRITE;
}

/**
 * State: TLS handshake
 */
static int step_tls_handshake(async_request_t *req) {
    /* Create SSL context if not exists */
    if (!req->ssl) {
        /* TODO: Get SSL_CTX from client */
        /* For now, create a minimal SSL object */
        printf("[async_request] TLS handshake needs SSL_CTX (id=%lu)\n",
               (unsigned long)req->id);

        /* Skip TLS for now until we integrate with client's SSL context */
        req->state = ASYNC_STATE_SENDING_REQUEST;
        return ASYNC_STATUS_IN_PROGRESS;
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
            async_request_set_error(req, err, "TLS handshake failed");
            return ASYNC_STATUS_ERROR;
    }
}

/**
 * State: Sending request
 */
static int step_sending_request(async_request_t *req) {
    /* Build request if not already done */
    if (req->send_len == 0) {
        /* TODO: Build HTTP request from req->request */
        /* For now, just a placeholder */
        printf("[async_request] Building HTTP request (id=%lu)\n",
               (unsigned long)req->id);

        /* Placeholder request */
        const char *placeholder = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
        req->send_len = strlen(placeholder);
        memcpy(req->send_buf, placeholder, req->send_len);
        req->send_pos = 0;
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

                /* TODO: Parse Content-Length, Transfer-Encoding, etc. */
                req->content_length = 0;  /* Placeholder */
                req->chunked_encoding = false;

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
