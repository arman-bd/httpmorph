/**
 * async_request.h - Async request state machine for non-blocking HTTP operations
 */

#ifndef ASYNC_REQUEST_H
#define ASYNC_REQUEST_H

#include "httpmorph.h"
#include "io_engine.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Forward declarations (avoid including OpenSSL headers here)
 */
typedef struct ssl_st SSL;



/**
 * Async request states
 */
typedef enum {
    ASYNC_STATE_INIT,                /* Initial state, not started yet */
    ASYNC_STATE_DNS_LOOKUP,          /* DNS resolution in progress */
    ASYNC_STATE_CONNECTING,          /* TCP connection in progress */
    ASYNC_STATE_TLS_HANDSHAKE,       /* TLS handshake in progress */
    ASYNC_STATE_SENDING_REQUEST,     /* Sending HTTP request */
    ASYNC_STATE_RECEIVING_HEADERS,   /* Receiving response headers */
    ASYNC_STATE_RECEIVING_BODY,      /* Receiving response body */
    ASYNC_STATE_COMPLETE,            /* Request completed successfully */
    ASYNC_STATE_ERROR                /* Request failed */
} async_request_state_t;

/**
 * Async request completion status
 */
typedef enum {
    ASYNC_STATUS_IN_PROGRESS = 0,    /* Operation in progress */
    ASYNC_STATUS_COMPLETE = 1,       /* Operation completed successfully */
    ASYNC_STATUS_ERROR = -1,         /* Operation failed */
    ASYNC_STATUS_NEED_READ = 2,      /* Needs to wait for read event */
    ASYNC_STATUS_NEED_WRITE = 3      /* Needs to wait for write event */
} async_request_status_t;

/**
 * Forward declarations
 */
typedef struct async_request async_request_t;

/**
 * Completion callback type
 */
typedef void (*async_request_callback_t)(async_request_t *req, int status);

/**
 * Async request structure
 */
struct async_request {
    /* Request ID */
    uint64_t id;

    /* Current state */
    async_request_state_t state;

    /* Request/response objects */
    httpmorph_request_t *request;
    httpmorph_response_t *response;

    /* Socket state */
    int sockfd;
    SSL *ssl;
    bool is_https;

    /* DNS resolution result */
    struct sockaddr_storage addr;
    socklen_t addr_len;
    bool dns_resolved;

    /* I/O operation tracking */
    io_operation_t *current_op;
    io_engine_t *io_engine;

    /* Send buffer state */
    uint8_t *send_buf;
    size_t send_len;
    size_t send_pos;

    /* Receive buffer state */
    uint8_t *recv_buf;
    size_t recv_capacity;
    size_t recv_len;

    /* Header parsing state */
    bool headers_complete;
    size_t headers_end_pos;

    /* Body receiving state */
    size_t content_length;
    size_t body_received;
    bool chunked_encoding;

    /* Timing and timeout */
    uint64_t start_time_us;
    uint64_t deadline_us;
    uint32_t timeout_ms;

    /* Error tracking */
    int error_code;
    char error_msg[256];

    /* Completion callback */
    async_request_callback_t on_complete;
    void *user_data;

    /* Reference counting */
    int refcount;
};

/* Forward declaration for SSL_CTX */
typedef struct ssl_ctx_st SSL_CTX;

/**
 * Create a new async request
 */
async_request_t* async_request_create(
    const httpmorph_request_t *request,
    io_engine_t *io_engine,
    SSL_CTX *ssl_ctx,
    uint32_t timeout_ms,
    async_request_callback_t callback,
    void *user_data
);

/**
 * Destroy an async request
 */
void async_request_destroy(async_request_t *req);

/**
 * Increment reference count
 */
void async_request_ref(async_request_t *req);

/**
 * Decrement reference count and destroy if zero
 */
void async_request_unref(async_request_t *req);

/**
 * Step the async request state machine
 * Returns:
 *   ASYNC_STATUS_IN_PROGRESS - Continue processing
 *   ASYNC_STATUS_COMPLETE - Request completed successfully
 *   ASYNC_STATUS_ERROR - Request failed
 *   ASYNC_STATUS_NEED_READ - Waiting for socket to be readable
 *   ASYNC_STATUS_NEED_WRITE - Waiting for socket to be writable
 */
int async_request_step(async_request_t *req);

/**
 * Get current state
 */
async_request_state_t async_request_get_state(const async_request_t *req);

/**
 * Get state name for debugging
 */
const char* async_request_state_name(async_request_state_t state);

/**
 * Get file descriptor for event loop integration
 */
int async_request_get_fd(const async_request_t *req);

/**
 * Check if request has timed out
 */
bool async_request_is_timeout(const async_request_t *req);

/**
 * Set error state
 */
void async_request_set_error(async_request_t *req, int error_code, const char *error_msg);

/**
 * Get response (only valid after completion)
 */
httpmorph_response_t* async_request_get_response(async_request_t *req);

/**
 * Get error message
 */
const char* async_request_get_error_message(const async_request_t *req);

#ifdef __cplusplus
}
#endif

#endif /* ASYNC_REQUEST_H */
