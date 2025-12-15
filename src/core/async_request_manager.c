/**
 * async_request_manager.c - Implementation of async request manager
 */

#include "async_request_manager.h"
#include "connection_pool.h"
#include "internal/tls.h"
#include "../tls/browser_profiles.h"
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

    /* Configure SSL context - use minimal configuration for async to avoid conflicts */
    SSL_CTX_set_verify(mgr->ssl_ctx, SSL_VERIFY_PEER, NULL);
#ifdef _WIN32
    /* On Windows, load certificates from Windows Certificate Store */
    httpmorph_load_windows_ca_certs(mgr->ssl_ctx);
#else
    /* On Unix-like systems, use default paths */
    SSL_CTX_set_default_verify_paths(mgr->ssl_ctx);
#endif

    /* Enable SSL session caching for TLS session resumption */
    SSL_CTX_set_session_cache_mode(mgr->ssl_ctx, SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_INTERNAL);
    SSL_CTX_set_timeout(mgr->ssl_ctx, 300);  /* 5 minute session timeout */

    /* Configure SSL_CTX with Chrome 143 browser profile for proper TLS fingerprinting.
     * This sets cipher suites, extensions, GREASE, ALPN, etc. to match Chrome's JA4 fingerprint.
     * Note: httpmorph_configure_ssl_ctx already configures ALPN for HTTP/2 from the profile. */
    httpmorph_configure_ssl_ctx(mgr->ssl_ctx, &PROFILE_CHROME_143);

    /* Create connection pool for reuse */
    mgr->pool = pool_create();
    if (!mgr->pool) {
        SSL_CTX_free(mgr->ssl_ctx);
        io_engine_destroy(mgr->io_engine);
        free(mgr);
        return NULL;
    }

    /* Allocate request array */
    mgr->request_capacity = INITIAL_CAPACITY;
    mgr->requests = calloc(mgr->request_capacity, sizeof(async_request_t*));
    if (!mgr->requests) {
        pool_destroy(mgr->pool);
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

    /* Destroy connection pool */
    if (mgr->pool) {
        pool_destroy(mgr->pool);
    }

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

    /* Create async request with connection pooling support */
    async_request_t *req = async_request_create_pooled(
        request,
        mgr->io_engine,
        mgr->ssl_ctx,
        mgr->pool,
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
    /* Note: The initial refcount=1 from creation IS the manager's reference */

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
 * Remove a completed request from the manager
 * Should be called after extracting the response to prevent memory leaks
 */
int async_manager_remove_request(
    async_request_manager_t *mgr,
    uint64_t request_id)
{
    if (!mgr) {
        return -1;
    }

    pthread_mutex_lock(&mgr->mutex);
    for (size_t i = 0; i < mgr->request_count; i++) {
        if (mgr->requests[i] && mgr->requests[i]->id == request_id) {
            async_request_t *req = mgr->requests[i];

            /* Remove from array by shifting remaining elements */
            for (size_t j = i; j < mgr->request_count - 1; j++) {
                mgr->requests[j] = mgr->requests[j + 1];
            }
            mgr->request_count--;
            mgr->requests[mgr->request_count] = NULL;

            /* Release manager's reference */
            async_request_unref(req);

            pthread_mutex_unlock(&mgr->mutex);
            DEBUG_PRINT("[async_manager] Removed request id=%lu\n", (unsigned long)request_id);
            return 0;
        }
    }
    pthread_mutex_unlock(&mgr->mutex);
    return -1;  /* Request not found */
}

/**
 * Poll for events
 * Note: Does NOT automatically clean up completed requests to avoid race conditions
 * with concurrent Python coroutines. Python must call async_manager_remove_request()
 * after extracting the response.
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

    /* NOTE: We intentionally do NOT clean up completed requests here.
     * Python coroutines must explicitly call async_manager_remove_request()
     * after extracting the response to avoid race conditions. */

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
