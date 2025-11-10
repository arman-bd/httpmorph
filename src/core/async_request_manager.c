/**
 * async_request_manager.c - Implementation of async request manager
 */

#include "async_request_manager.h"
#include "internal/tls.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* OpenSSL */
#include <openssl/ssl.h>
#include <openssl/err.h>

/* Debug output control */
#ifdef HTTPMORPH_DEBUG
    #define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
    #define DEBUG_PRINT(...) ((void)0)
#endif

/* Initial capacity for request array */
#define INITIAL_CAPACITY 16

/* Forward declarations */
static void cleanup_completed_requests(async_request_manager_t *mgr);

/**
 * Create a new async request manager
 */
async_request_manager_t* async_manager_create(void) {
    async_request_manager_t *mgr = calloc(1, sizeof(async_request_manager_t));
    if (!mgr) {
        return NULL;
    }

    /* Create I/O engine */
    mgr->io_engine = io_engine_create(256);  /* Queue depth 256 */
    if (!mgr->io_engine) {
        free(mgr);
        return NULL;
    }

    /* Create SSL context */
    mgr->ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (!mgr->ssl_ctx) {
        io_engine_destroy(mgr->io_engine);
        free(mgr);
        return NULL;
    }

    /* Configure SSL context */
    SSL_CTX_set_verify(mgr->ssl_ctx, SSL_VERIFY_PEER, NULL);
#ifdef _WIN32
    /* On Windows, load certificates from Windows Certificate Store */
    httpmorph_load_windows_ca_certs(mgr->ssl_ctx);
#else
    /* On Unix-like systems, use default paths */
    SSL_CTX_set_default_verify_paths(mgr->ssl_ctx);
#endif

    /* Allocate request array */
    mgr->request_capacity = INITIAL_CAPACITY;
    mgr->requests = calloc(mgr->request_capacity, sizeof(async_request_t*));
    if (!mgr->requests) {
        SSL_CTX_free(mgr->ssl_ctx);
        io_engine_destroy(mgr->io_engine);
        free(mgr);
        return NULL;
    }

    /* Initialize mutex */
    pthread_mutex_init(&mgr->mutex, NULL);

    /* Initialize ID counter */
    mgr->next_request_id = 1;

    DEBUG_PRINT("[async_manager] Created with I/O engine and SSL context\n");
    return mgr;
}

/**
 * Destroy an async request manager
 */
void async_manager_destroy(async_request_manager_t *mgr) {
    if (!mgr) {
        return;
    }

    /* Stop event loop if running */
    if (mgr->event_thread_running) {
        async_manager_stop_event_loop(mgr);
    }

    DEBUG_PRINT("[async_manager] Graceful shutdown: waiting for %zu active requests\n", mgr->request_count);

    /* Graceful shutdown: Wait for all active requests to complete or timeout */
    pthread_mutex_lock(&mgr->mutex);
    int wait_iterations = 0;
    const int max_wait_iterations = 100;  /* 10 seconds max (100 * 100ms) */

    while (mgr->request_count > 0 && wait_iterations < max_wait_iterations) {
        pthread_mutex_unlock(&mgr->mutex);

        /* Give requests time to complete */
        struct timespec ts = {0, 100000000};  /* 100ms */
        nanosleep(&ts, NULL);

        /* Step all requests to allow them to complete */
        pthread_mutex_lock(&mgr->mutex);
        for (size_t i = 0; i < mgr->request_count; i++) {
            if (mgr->requests[i]) {
                async_request_state_t state = async_request_get_state(mgr->requests[i]);

                /* For requests still in progress, step them */
                if (state != ASYNC_STATE_COMPLETE && state != ASYNC_STATE_ERROR) {
                    async_request_step(mgr->requests[i]);
                }
            }
        }

        /* Clean up completed requests */
        cleanup_completed_requests(mgr);

        wait_iterations++;

        if (mgr->request_count > 0 && wait_iterations % 10 == 0) {
            DEBUG_PRINT("[async_manager] Still waiting for %zu requests (iteration %d)\n",
                   mgr->request_count, wait_iterations);
        }
    }

    /* Force cleanup of any remaining requests */
    if (mgr->request_count > 0) {
        DEBUG_PRINT("[async_manager] Force cleanup of %zu remaining requests\n", mgr->request_count);
        for (size_t i = 0; i < mgr->request_count; i++) {
            if (mgr->requests[i]) {
                /* Set error state for incomplete requests */
                async_request_state_t state = async_request_get_state(mgr->requests[i]);
                if (state != ASYNC_STATE_COMPLETE && state != ASYNC_STATE_ERROR) {
                    async_request_set_error(mgr->requests[i], -1, "Manager shutdown");
                }
                async_request_unref(mgr->requests[i]);
            }
        }
    }

    free(mgr->requests);
    pthread_mutex_unlock(&mgr->mutex);

    /* Destroy SSL context */
    if (mgr->ssl_ctx) {
        SSL_CTX_free(mgr->ssl_ctx);
    }

    /* Destroy I/O engine */
    io_engine_destroy(mgr->io_engine);

    /* Destroy mutex */
    pthread_mutex_destroy(&mgr->mutex);

    DEBUG_PRINT("[async_manager] Destroyed\n");
    free(mgr);
}

/**
 * Grow request array if needed
 */
static int grow_request_array(async_request_manager_t *mgr) {
    /* Check for integer overflow before doubling */
    if (mgr->request_capacity > SIZE_MAX / 2 / sizeof(async_request_t*)) {
        /* Would overflow - reject new request */
        return -1;
    }

    size_t new_capacity = mgr->request_capacity * 2;
    async_request_t **new_array = realloc(mgr->requests,
                                          new_capacity * sizeof(async_request_t*));
    if (!new_array) {
        return -1;
    }

    /* Zero new entries */
    memset(new_array + mgr->request_capacity, 0,
           (new_capacity - mgr->request_capacity) * sizeof(async_request_t*));

    mgr->requests = new_array;
    mgr->request_capacity = new_capacity;
    return 0;
}

/**
 * Submit a new async request
 */
uint64_t async_manager_submit_request(
    async_request_manager_t *mgr,
    const httpmorph_request_t *request,
    uint32_t timeout_ms,
    async_request_callback_t callback,
    void *user_data)
{
    if (!mgr || !request) {
        return 0;
    }

    pthread_mutex_lock(&mgr->mutex);

    /* Create async request */
    async_request_t *req = async_request_create(
        request,
        mgr->io_engine,
        mgr->ssl_ctx,
        timeout_ms,
        callback,
        user_data
    );

    if (!req) {
        pthread_mutex_unlock(&mgr->mutex);
        return 0;
    }

    /* Assign ID */
    uint64_t request_id = mgr->next_request_id++;
    req->id = request_id;

    /* Grow array if needed */
    if (mgr->request_count >= mgr->request_capacity) {
        if (grow_request_array(mgr) < 0) {
            async_request_unref(req);
            pthread_mutex_unlock(&mgr->mutex);
            return 0;
        }
    }

    /* Add to array */
    mgr->requests[mgr->request_count++] = req;
    async_request_ref(req);  /* Manager holds a reference */

    pthread_mutex_unlock(&mgr->mutex);

    DEBUG_PRINT("[async_manager] Submitted request id=%lu\n", (unsigned long)request_id);
    return request_id;
}

/**
 * Get request by ID
 */
async_request_t* async_manager_get_request(
    async_request_manager_t *mgr,
    uint64_t request_id)
{
    if (!mgr) {
        return NULL;
    }

    pthread_mutex_lock(&mgr->mutex);
    for (size_t i = 0; i < mgr->request_count; i++) {
        if (mgr->requests[i] && mgr->requests[i]->id == request_id) {
            async_request_t *req = mgr->requests[i];
            async_request_ref(req);  /* Caller gets a reference */
            pthread_mutex_unlock(&mgr->mutex);
            return req;
        }
    }
    pthread_mutex_unlock(&mgr->mutex);
    return NULL;
}

/**
 * Remove completed/failed requests
 */
static void cleanup_completed_requests(async_request_manager_t *mgr) {
    size_t write_pos = 0;

    for (size_t i = 0; i < mgr->request_count; i++) {
        async_request_t *req = mgr->requests[i];
        async_request_state_t state = async_request_get_state(req);

        if (state == ASYNC_STATE_COMPLETE || state == ASYNC_STATE_ERROR) {
            /* Release manager's reference */
            async_request_unref(req);
        } else {
            /* Keep this request */
            mgr->requests[write_pos++] = req;
        }
    }

    mgr->request_count = write_pos;
}

/**
 * Cancel a request
 */
int async_manager_cancel_request(
    async_request_manager_t *mgr,
    uint64_t request_id)
{
    if (!mgr) {
        return -1;
    }

    pthread_mutex_lock(&mgr->mutex);
    for (size_t i = 0; i < mgr->request_count; i++) {
        if (mgr->requests[i] && mgr->requests[i]->id == request_id) {
            async_request_set_error(mgr->requests[i], -1, "Cancelled");
            pthread_mutex_unlock(&mgr->mutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&mgr->mutex);
    return -1;
}

/**
 * Poll for events
 */
int async_manager_poll(async_request_manager_t *mgr, uint32_t timeout_ms) {
    if (!mgr) {
        return -1;
    }

    /* Wait for I/O events */
    int events = io_engine_wait(mgr->io_engine, timeout_ms);

    pthread_mutex_lock(&mgr->mutex);

    /* Process all active requests */
    for (size_t i = 0; i < mgr->request_count; i++) {
        async_request_t *req = mgr->requests[i];
        if (!req) {
            continue;
        }

        /* Step the state machine */
        int status = async_request_step(req);

        /* Register for events based on status */
        if (status == ASYNC_STATUS_NEED_READ || status == ASYNC_STATUS_NEED_WRITE) {
            int fd = async_request_get_fd(req);
            if (fd >= 0) {
                /* Create I/O operation */
                io_operation_t *op = NULL;
                if (status == ASYNC_STATUS_NEED_READ) {
                    op = io_op_recv_create(fd, NULL, 0, NULL, req);
                } else {
                    op = io_op_send_create(fd, NULL, 0, NULL, req);
                }

                if (op) {
                    io_engine_submit(mgr->io_engine, op);
                }
            }
        }
    }

    /* Clean up completed requests */
    cleanup_completed_requests(mgr);

    pthread_mutex_unlock(&mgr->mutex);

    return events;
}

/**
 * Process all pending requests
 */
int async_manager_process(async_request_manager_t *mgr) {
    if (!mgr) {
        return -1;
    }

    int processed = 0;

    while (mgr->request_count > 0) {
        /* Poll with 100ms timeout */
        int events = async_manager_poll(mgr, 100);
        if (events > 0) {
            processed += events;
        }
    }

    return processed;
}

/**
 * Get number of active requests
 */
size_t async_manager_get_active_count(const async_request_manager_t *mgr) {
    if (!mgr) {
        return 0;
    }
    return mgr->request_count;
}

/**
 * Event loop thread function
 */
static void* event_loop_thread(void *arg) {
    async_request_manager_t *mgr = (async_request_manager_t*)arg;

    DEBUG_PRINT("[async_manager] Event loop thread started\n");

    while (!mgr->shutdown) {
        async_manager_poll(mgr, 100);  /* 100ms timeout */
    }

    DEBUG_PRINT("[async_manager] Event loop thread stopped\n");
    return NULL;
}

/**
 * Start event loop thread
 */
int async_manager_start_event_loop(async_request_manager_t *mgr) {
    if (!mgr || mgr->event_thread_running) {
        return -1;
    }

    mgr->shutdown = false;
    if (pthread_create(&mgr->event_thread, NULL, event_loop_thread, mgr) != 0) {
        return -1;
    }

    mgr->event_thread_running = true;
    return 0;
}

/**
 * Stop event loop thread
 */
int async_manager_stop_event_loop(async_request_manager_t *mgr) {
    if (!mgr || !mgr->event_thread_running) {
        return -1;
    }

    mgr->shutdown = true;
    pthread_join(mgr->event_thread, NULL);
    mgr->event_thread_running = false;
    return 0;
}
