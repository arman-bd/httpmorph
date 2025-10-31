/**
 * http2_session_manager.h - HTTP/2 Session Manager for Concurrent Multiplexing
 *
 * Manages concurrent HTTP/2 streams on a single session.
 * Allows multiple threads to submit requests and share one HTTP/2 connection.
 */

#ifndef HTTPMORPH_HTTP2_SESSION_MANAGER_H
#define HTTPMORPH_HTTP2_SESSION_MANAGER_H

#ifdef HAVE_NGHTTP2

#include <stdint.h>
#include <stdbool.h>
#ifndef _WIN32
    #include <pthread.h>
#endif
#include <nghttp2/nghttp2.h>
#include <openssl/ssl.h>

/* Forward declarations */
typedef struct http2_session_manager http2_session_manager_t;
typedef struct http2_pending_stream http2_pending_stream_t;

/**
 * Pending stream structure
 * Tracks a single stream until completion
 */
struct http2_pending_stream {
    int32_t stream_id;
    void *stream_data;            /* http2_stream_data_t* - avoid circular dependency */

    /* Synchronization for this specific stream */
    pthread_mutex_t stream_mutex;
    pthread_cond_t stream_complete;
    bool completed;
    bool has_error;

    /* Linked list */
    http2_pending_stream_t *next;
};

/**
 * HTTP/2 Session Manager
 * Coordinates concurrent access to a single HTTP/2 session
 */
struct http2_session_manager {
    /* HTTP/2 session */
    nghttp2_session *session;
    nghttp2_session_callbacks *callbacks;

    /* Connection */
    SSL *ssl;
    int sockfd;

    /* I/O thread management */
    pthread_t io_thread;
    pthread_mutex_t mutex;           /* Protects session and stream list */
    bool io_thread_running;
    bool shutdown_requested;

    /* Stream tracking */
    http2_pending_stream_t *pending_streams;
    int active_stream_count;

    /* Statistics */
    uint64_t total_streams_submitted;
    uint64_t total_streams_completed;
};

/* === Session Manager Lifecycle === */

/**
 * Create a new HTTP/2 session manager
 *
 * @param session Existing nghttp2_session (takes ownership)
 * @param callbacks nghttp2 callbacks (takes ownership)
 * @param ssl SSL connection
 * @param sockfd Socket file descriptor
 * @return New session manager or NULL on error
 */
http2_session_manager_t* http2_session_manager_create(
    nghttp2_session *session,
    nghttp2_session_callbacks *callbacks,
    SSL *ssl,
    int sockfd
);

/**
 * Destroy a session manager
 * Stops I/O thread, cleans up resources
 *
 * @param mgr Session manager to destroy
 */
void http2_session_manager_destroy(http2_session_manager_t *mgr);

/**
 * Start the I/O thread
 * Must be called before submitting streams
 *
 * @param mgr Session manager
 * @return 0 on success, -1 on error
 */
int http2_session_manager_start(http2_session_manager_t *mgr);

/**
 * Stop the I/O thread
 * Waits for thread to finish
 *
 * @param mgr Session manager
 */
void http2_session_manager_stop(http2_session_manager_t *mgr);

/* === Stream Operations === */

/**
 * Submit a new HTTP/2 stream
 * Non-blocking operation that queues the stream
 *
 * @param mgr Session manager
 * @param stream_data Stream-specific data (takes ownership) - void* to http2_stream_data_t*
 * @param pri_spec Priority specification (can be NULL for default priority)
 * @param hdrs Request headers
 * @param hdr_count Number of headers
 * @param data_prd Data provider for request body (can be NULL)
 * @param stream_id_out Output parameter for assigned stream ID
 * @return 0 on success, -1 on error
 */
int http2_session_manager_submit_stream(
    http2_session_manager_t *mgr,
    void *stream_data,
    const nghttp2_priority_spec *pri_spec,
    const nghttp2_nv *hdrs,
    size_t hdr_count,
    nghttp2_data_provider *data_prd,
    int32_t *stream_id_out
);

/**
 * Wait for a stream to complete
 * Blocking operation with timeout
 *
 * @param mgr Session manager
 * @param stream_id Stream ID to wait for
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, -1 on timeout or error
 */
int http2_session_manager_wait_for_stream(
    http2_session_manager_t *mgr,
    int32_t stream_id,
    uint32_t timeout_ms
);

/**
 * Remove a completed stream from tracking
 * Frees resources associated with the stream
 *
 * @param mgr Session manager
 * @param stream_id Stream ID to remove
 */
void http2_session_manager_remove_stream(
    http2_session_manager_t *mgr,
    int32_t stream_id
);

/* === Internal Helpers (used by callbacks) === */

/**
 * Mark a stream as completed
 * Called by nghttp2 callbacks when stream finishes
 *
 * @param mgr Session manager
 * @param stream_id Stream ID that completed
 * @param has_error Whether the stream had an error
 */
void http2_session_manager_mark_stream_complete(
    http2_session_manager_t *mgr,
    int32_t stream_id,
    bool has_error
);

/**
 * Find a pending stream by ID
 * Must be called with mgr->mutex held
 *
 * @param mgr Session manager
 * @param stream_id Stream ID to find
 * @return Pending stream or NULL if not found
 */
http2_pending_stream_t* http2_session_manager_find_stream(
    http2_session_manager_t *mgr,
    int32_t stream_id
);

#endif /* HAVE_NGHTTP2 */

#endif /* HTTPMORPH_HTTP2_SESSION_MANAGER_H */
