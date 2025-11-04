/**
 * http2_session_manager.c - HTTP/2 Session Manager Implementation
 */

/* Include internal.h first to get POSIX feature test macros */
#include "internal/internal.h"
#include "http2_session_manager.h"

#ifdef HAVE_NGHTTP2

#include "internal/http2_logic.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#include <sys/select.h>
#endif

/* Helper: Create a pending stream tracker */
static http2_pending_stream_t* create_pending_stream(
    int32_t stream_id,
    void *stream_data
) {
    http2_pending_stream_t *pending = calloc(1, sizeof(http2_pending_stream_t));
    if (!pending) return NULL;

    pending->stream_id = stream_id;
    pending->stream_data = stream_data;
    pending->completed = false;
    pending->has_error = false;
    pending->next = NULL;

    pthread_mutex_init(&pending->stream_mutex, NULL);
    pthread_cond_init(&pending->stream_complete, NULL);

    return pending;
}

/* Helper: Destroy a pending stream */
static void destroy_pending_stream(http2_pending_stream_t *pending) {
    if (!pending) return;

    pthread_mutex_destroy(&pending->stream_mutex);
    pthread_cond_destroy(&pending->stream_complete);
    free(pending);
}

/* Helper: Add pending stream to manager's list */
static void add_pending_stream(
    http2_session_manager_t *mgr,
    http2_pending_stream_t *pending
) {
    /* mgr->mutex must be held by caller */
    pending->next = mgr->pending_streams;
    mgr->pending_streams = pending;
    mgr->active_stream_count++;
}

/**
 * Find a pending stream by ID
 */
http2_pending_stream_t* http2_session_manager_find_stream(
    http2_session_manager_t *mgr,
    int32_t stream_id
) {
    /* mgr->mutex must be held by caller */
    http2_pending_stream_t *curr = mgr->pending_streams;
    while (curr) {
        if (curr->stream_id == stream_id) {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

/**
 * I/O thread main loop
 * Handles send/recv for all streams on this session
 */
static void* http2_io_thread_main(void *arg) {
    http2_session_manager_t *mgr = (http2_session_manager_t*)arg;

    while (!mgr->shutdown_requested) {
        pthread_mutex_lock(&mgr->mutex);

        /* Send any pending data */
        int rv = nghttp2_session_send(mgr->session);
        if (rv != 0) {
            /* Send error - might need to shutdown */
            pthread_mutex_unlock(&mgr->mutex);
            break;
        }

        pthread_mutex_unlock(&mgr->mutex);

        /* Receive data with select for efficiency */
        fd_set readfds;
        struct timeval tv;

        FD_ZERO(&readfds);
        FD_SET(mgr->sockfd, &readfds);

        tv.tv_sec = 0;
        tv.tv_usec = 10000;  /* 10ms timeout */

        int select_rv = select(mgr->sockfd + 1, &readfds, NULL, NULL, &tv);

        if (select_rv > 0 && FD_ISSET(mgr->sockfd, &readfds)) {
            pthread_mutex_lock(&mgr->mutex);

            /* Receive data */
            rv = nghttp2_session_recv(mgr->session);

            if (rv == NGHTTP2_ERR_EOF) {
                /* Connection closed */
                pthread_mutex_unlock(&mgr->mutex);
                break;
            } else if (rv < 0 && rv != NGHTTP2_ERR_WOULDBLOCK) {
                /* Receive error */
                pthread_mutex_unlock(&mgr->mutex);
                break;
            }

            pthread_mutex_unlock(&mgr->mutex);
        }

        /* Small sleep to avoid busy-waiting if no data */
        if (select_rv == 0) {
            usleep(1000);  /* 1ms */
        }
    }

    /* Mark all remaining streams as failed */
    pthread_mutex_lock(&mgr->mutex);
    http2_pending_stream_t *curr = mgr->pending_streams;
    while (curr) {
        pthread_mutex_lock(&curr->stream_mutex);
        curr->completed = true;
        curr->has_error = true;
        pthread_cond_signal(&curr->stream_complete);
        pthread_mutex_unlock(&curr->stream_mutex);
        curr = curr->next;
    }
    pthread_mutex_unlock(&mgr->mutex);

    return NULL;
}

/**
 * Create a new HTTP/2 session manager
 */
http2_session_manager_t* http2_session_manager_create(
    nghttp2_session *session,
    nghttp2_session_callbacks *callbacks,
    SSL *ssl,
    int sockfd
) {
    if (!session || !ssl) {
        return NULL;
    }

    http2_session_manager_t *mgr = calloc(1, sizeof(http2_session_manager_t));
    if (!mgr) {
        return NULL;
    }

    mgr->session = session;
    mgr->callbacks = callbacks;
    mgr->ssl = ssl;
    mgr->sockfd = sockfd;
    mgr->pending_streams = NULL;
    mgr->active_stream_count = 0;
    mgr->total_streams_submitted = 0;
    mgr->total_streams_completed = 0;
    mgr->io_thread_running = false;
    mgr->shutdown_requested = false;

    pthread_mutex_init(&mgr->mutex, NULL);

    return mgr;
}

/**
 * Destroy a session manager
 */
void http2_session_manager_destroy(http2_session_manager_t *mgr) {
    if (!mgr) return;

    /* Stop I/O thread if running */
    if (mgr->io_thread_running) {
        http2_session_manager_stop(mgr);
    }

    /* Clean up pending streams */
    http2_pending_stream_t *curr = mgr->pending_streams;
    while (curr) {
        http2_pending_stream_t *next = curr->next;
        destroy_pending_stream(curr);
        curr = next;
    }

    /* Note: We don't destroy session/callbacks here as they may be owned by pooled_connection */

    pthread_mutex_destroy(&mgr->mutex);
    free(mgr);
}

/**
 * Start the I/O thread
 */
int http2_session_manager_start(http2_session_manager_t *mgr) {
    if (!mgr || mgr->io_thread_running) {
        return -1;
    }

    int rv = pthread_create(&mgr->io_thread, NULL, http2_io_thread_main, mgr);
    if (rv != 0) {
        return -1;
    }

    mgr->io_thread_running = true;
    return 0;
}

/**
 * Stop the I/O thread
 */
void http2_session_manager_stop(http2_session_manager_t *mgr) {
    if (!mgr || !mgr->io_thread_running) {
        return;
    }

    mgr->shutdown_requested = true;

    /* Wait for thread to finish */
    pthread_join(mgr->io_thread, NULL);

    mgr->io_thread_running = false;
}

/**
 * Submit a new HTTP/2 stream
 */
int http2_session_manager_submit_stream(
    http2_session_manager_t *mgr,
    void *stream_data,
    const nghttp2_priority_spec *pri_spec,
    const nghttp2_nv *hdrs,
    size_t hdr_count,
    nghttp2_data_provider *data_prd,
    int32_t *stream_id_out
) {
    if (!mgr || !stream_data || !hdrs) {
        return -1;
    }

    pthread_mutex_lock(&mgr->mutex);

    /* Submit stream to nghttp2 with priority spec */
    int32_t stream_id = nghttp2_submit_request(
        mgr->session,
        pri_spec,  /* Priority spec (NULL for default) */
        hdrs,
        hdr_count,
        data_prd,
        stream_data  /* User data for callbacks */
    );

    if (stream_id < 0) {
        pthread_mutex_unlock(&mgr->mutex);
        return -1;
    }

    /* Create pending stream tracker */
    http2_pending_stream_t *pending = create_pending_stream(stream_id, stream_data);
    if (!pending) {
        pthread_mutex_unlock(&mgr->mutex);
        return -1;
    }

    /* Add to tracking list */
    add_pending_stream(mgr, pending);
    mgr->total_streams_submitted++;

    *stream_id_out = stream_id;

    pthread_mutex_unlock(&mgr->mutex);

    return 0;
}

/**
 * Wait for a stream to complete
 */
int http2_session_manager_wait_for_stream(
    http2_session_manager_t *mgr,
    int32_t stream_id,
    uint32_t timeout_ms
) {
    if (!mgr) {
        return -1;
    }

    /* Find pending stream */
    pthread_mutex_lock(&mgr->mutex);
    http2_pending_stream_t *pending = http2_session_manager_find_stream(mgr, stream_id);
    pthread_mutex_unlock(&mgr->mutex);

    if (!pending) {
        return -1;  /* Stream not found */
    }

    /* Wait for completion with timeout */
    pthread_mutex_lock(&pending->stream_mutex);

    if (!pending->completed) {
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += timeout_ms / 1000;
        timeout.tv_nsec += (timeout_ms % 1000) * 1000000;

        /* Handle nanosecond overflow */
        if (timeout.tv_nsec >= 1000000000) {
            timeout.tv_sec += 1;
            timeout.tv_nsec -= 1000000000;
        }

        while (!pending->completed) {
            int rv = pthread_cond_timedwait(
                &pending->stream_complete,
                &pending->stream_mutex,
                &timeout
            );

            if (rv == ETIMEDOUT) {
                pthread_mutex_unlock(&pending->stream_mutex);
                return -1;  /* Timeout */
            } else if (rv != 0 && rv != EINTR) {
                pthread_mutex_unlock(&pending->stream_mutex);
                return -1;  /* Error */
            }
        }
    }

    int result = pending->has_error ? -1 : 0;

    pthread_mutex_unlock(&pending->stream_mutex);

    return result;
}

/**
 * Remove a completed stream from tracking
 */
void http2_session_manager_remove_stream(
    http2_session_manager_t *mgr,
    int32_t stream_id
) {
    if (!mgr) return;

    pthread_mutex_lock(&mgr->mutex);

    /* Find and remove from list */
    http2_pending_stream_t **curr = &mgr->pending_streams;
    while (*curr) {
        if ((*curr)->stream_id == stream_id) {
            http2_pending_stream_t *to_remove = *curr;
            *curr = to_remove->next;
            mgr->active_stream_count--;
            destroy_pending_stream(to_remove);
            break;
        }
        curr = &(*curr)->next;
    }

    pthread_mutex_unlock(&mgr->mutex);
}

/**
 * Mark a stream as completed
 */
void http2_session_manager_mark_stream_complete(
    http2_session_manager_t *mgr,
    int32_t stream_id,
    bool has_error
) {
    if (!mgr) return;

    pthread_mutex_lock(&mgr->mutex);

    http2_pending_stream_t *pending = http2_session_manager_find_stream(mgr, stream_id);

    if (pending) {
        pthread_mutex_lock(&pending->stream_mutex);
        pending->completed = true;
        pending->has_error = has_error;
        pthread_cond_signal(&pending->stream_complete);
        pthread_mutex_unlock(&pending->stream_mutex);

        mgr->total_streams_completed++;
    }

    pthread_mutex_unlock(&mgr->mutex);
}

#endif /* HAVE_NGHTTP2 */
