/**
 * async_request_manager.h - Manager for multiple concurrent async requests
 */

#ifndef ASYNC_REQUEST_MANAGER_H
#define ASYNC_REQUEST_MANAGER_H

#include "async_request.h"
#include "io_engine.h"
#include <stdint.h>
#include <stdbool.h>

#ifndef _WIN32
    #include <pthread.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration for SSL_CTX */
typedef struct ssl_ctx_st SSL_CTX;

/**
 * Request manager structure
 */
typedef struct async_request_manager {
    /* I/O engine for event-driven I/O */
    io_engine_t *io_engine;

    /* SSL/TLS context */
    SSL_CTX *ssl_ctx;

    /* Request tracking */
    async_request_t **requests;
    size_t request_count;
    size_t request_capacity;

    /* Request ID generation */
    uint64_t next_request_id;

    /* Thread safety */
    pthread_mutex_t mutex;

    /* Event loop (optional - for standalone mode) */
    pthread_t event_thread;
    bool event_thread_running;
    bool shutdown;

} async_request_manager_t;

/**
 * Create a new async request manager
 */
async_request_manager_t* async_manager_create(void);

/**
 * Destroy an async request manager
 */
void async_manager_destroy(async_request_manager_t *mgr);

/**
 * Submit a new async request
 * Returns request ID (>0 on success, 0 on failure)
 */
uint64_t async_manager_submit_request(
    async_request_manager_t *mgr,
    const httpmorph_request_t *request,
    uint32_t timeout_ms,
    async_request_callback_t callback,
    void *user_data
);

/**
 * Get request by ID
 */
async_request_t* async_manager_get_request(
    async_request_manager_t *mgr,
    uint64_t request_id
);

/**
 * Cancel a request
 */
int async_manager_cancel_request(
    async_request_manager_t *mgr,
    uint64_t request_id
);

/**
 * Poll for events (non-blocking)
 * Returns number of events processed
 */
int async_manager_poll(
    async_request_manager_t *mgr,
    uint32_t timeout_ms
);

/**
 * Process all pending requests
 * This is the main event loop function
 */
int async_manager_process(async_request_manager_t *mgr);

/**
 * Get number of active requests
 */
size_t async_manager_get_active_count(const async_request_manager_t *mgr);

/**
 * Start event loop thread (optional)
 */
int async_manager_start_event_loop(async_request_manager_t *mgr);

/**
 * Stop event loop thread
 */
int async_manager_stop_event_loop(async_request_manager_t *mgr);

#ifdef __cplusplus
}
#endif

#endif /* ASYNC_REQUEST_MANAGER_H */
