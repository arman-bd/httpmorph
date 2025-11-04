/**
 * http2_logic.c - HTTP/2 protocol implementation
 */

#include "internal/http2_logic.h"

#ifdef HAVE_NGHTTP2

#include "internal/request.h"
#include "internal/response.h"
#include "connection_pool.h"
#include "http2_session_manager.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/select.h>
    #include <sys/socket.h>
#endif

/* Data structure for collecting HTTP/2 response */
typedef struct {
    httpmorph_response_t *response;
    uint8_t *data_buf;        /* Response body buffer */
    size_t data_capacity;
    size_t data_len;
    bool headers_complete;
    bool stream_closed;
    SSL *ssl;                 /* SSL connection for send/recv */

    /* Request body fields */
    const uint8_t *req_body;  /* Request body to send */
    size_t req_body_len;      /* Total length of request body */
    size_t req_body_sent;     /* Bytes already sent */

    /* Session manager for concurrent multiplexing */
    void *session_manager;    /* http2_session_manager_t* (void* to avoid circular dependency) */
    int32_t stream_id;        /* Stream ID for this request */
} http2_stream_data_t;

/* Helper: Send data over SSL or socket */
static ssize_t http2_send_callback(nghttp2_session *session, const uint8_t *data,
                                    size_t length, int flags, void *user_data) {
    http2_stream_data_t *stream_data = (http2_stream_data_t *)user_data;
    if (stream_data && stream_data->ssl) {
        return SSL_write(stream_data->ssl, data, length);
    }
    return -1;
}

/* Helper: Data provider callback for sending request body */
static ssize_t http2_data_source_read_callback(nghttp2_session *session, int32_t stream_id,
                                                 uint8_t *buf, size_t length, uint32_t *data_flags,
                                                 nghttp2_data_source *source, void *user_data) {
    http2_stream_data_t *stream_data = (http2_stream_data_t *)user_data;

    size_t remaining = stream_data->req_body_len - stream_data->req_body_sent;
    size_t to_send = remaining < length ? remaining : length;

    if (to_send > 0) {
        memcpy(buf, stream_data->req_body + stream_data->req_body_sent, to_send);
        stream_data->req_body_sent += to_send;
    }

    /* Set EOF flag if all data sent */
    if (stream_data->req_body_sent >= stream_data->req_body_len) {
        *data_flags |= NGHTTP2_DATA_FLAG_EOF;
    }

    return to_send;
}

/* Helper: Receive data from SSL or socket */
static ssize_t http2_recv_callback(nghttp2_session *session, uint8_t *buf,
                                    size_t length, int flags, void *user_data) {
    http2_stream_data_t *stream_data = (http2_stream_data_t *)user_data;
    if (stream_data && stream_data->ssl) {
        int n = SSL_read(stream_data->ssl, buf, length);
        if (n < 0) {
            int ssl_err = SSL_get_error(stream_data->ssl, n);
            if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
                return NGHTTP2_ERR_WOULDBLOCK;
            }
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        } else if (n == 0) {
            /* EOF - remote peer closed connection */
            return NGHTTP2_ERR_EOF;
        }
        return n;
    }
    return NGHTTP2_ERR_CALLBACK_FAILURE;
}

/* Helper: Called when a header name/value pair is received */
static int http2_on_header_callback(nghttp2_session *session,
                                     const nghttp2_frame *frame,
                                     const uint8_t *name, size_t namelen,
                                     const uint8_t *value, size_t valuelen,
                                     uint8_t flags, void *user_data) {
    /* For session reuse: try to get stream user data first, fallback to session user data */
    http2_stream_data_t *stream_data = (http2_stream_data_t *)nghttp2_session_get_stream_user_data(session, frame->hd.stream_id);
    if (!stream_data) {
        stream_data = (http2_stream_data_t *)user_data;
    }

    if (frame->hd.type != NGHTTP2_HEADERS || frame->headers.cat != NGHTTP2_HCAT_RESPONSE) {
        return 0;
    }

    /* Handle :status pseudo-header */
    if (namelen == 7 && memcmp(name, ":status", 7) == 0) {
        char status_str[4] = {0};
        size_t copy_len = valuelen > 3 ? 3 : valuelen;
        memcpy(status_str, value, copy_len);
        stream_data->response->status_code = atoi(status_str);
        return 0;
    }

    /* Add regular header */
    httpmorph_response_add_header_internal(stream_data->response, (const char *)name,
                                   namelen, (const char *)value, valuelen);
    return 0;
}

/* Helper: Called when DATA frame is received */
static int http2_on_data_chunk_recv_callback(nghttp2_session *session, uint8_t flags,
                                               int32_t stream_id, const uint8_t *data,
                                               size_t len, void *user_data) {
    /* For session reuse: try to get stream user data first, fallback to session user data */
    http2_stream_data_t *stream_data = (http2_stream_data_t *)nghttp2_session_get_stream_user_data(session, stream_id);
    if (!stream_data) {
        stream_data = (http2_stream_data_t *)user_data;
    }

    /* Expand buffer if needed */
    if (stream_data->data_len + len > stream_data->data_capacity) {
        size_t new_capacity = (stream_data->data_len + len) * 2;
        uint8_t *new_buf = realloc(stream_data->data_buf, new_capacity);
        if (!new_buf) return NGHTTP2_ERR_CALLBACK_FAILURE;
        stream_data->data_buf = new_buf;
        stream_data->data_capacity = new_capacity;
    }

    memcpy(stream_data->data_buf + stream_data->data_len, data, len);
    stream_data->data_len += len;
    return 0;
}

/* Helper: Called when a frame is received */
static int http2_on_frame_recv_callback(nghttp2_session *session,
                                         const nghttp2_frame *frame, void *user_data) {
    /* For session reuse: try to get stream user data first, fallback to session user data */
    http2_stream_data_t *stream_data = NULL;
    if (frame->hd.stream_id > 0) {
        stream_data = (http2_stream_data_t *)nghttp2_session_get_stream_user_data(session, frame->hd.stream_id);
    }
    if (!stream_data) {
        stream_data = (http2_stream_data_t *)user_data;
    }

    if (frame->hd.type == NGHTTP2_HEADERS &&
        frame->headers.cat == NGHTTP2_HCAT_RESPONSE) {
        stream_data->headers_complete = true;
    }

    /* Check if stream is closed - only check END_STREAM on HEADERS or DATA frames */
    if ((frame->hd.type == NGHTTP2_HEADERS || frame->hd.type == NGHTTP2_DATA) &&
        (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) &&
        frame->hd.stream_id > 0) {  /* Only for non-zero stream IDs */
        stream_data->stream_closed = true;

        /* Signal session manager if we're using concurrent mode */
        if (stream_data->session_manager) {
            http2_session_manager_t *mgr = (http2_session_manager_t*)stream_data->session_manager;
            http2_session_manager_mark_stream_complete(mgr, frame->hd.stream_id, false);
        }
    }
    return 0;
}

/* Helper: Initialize or reuse HTTP/2 session with stream-specific data */
static int http2_init_or_reuse_session(nghttp2_session **session_ptr,
                                        nghttp2_session_callbacks *callbacks,
                                        http2_stream_data_t *stream_data,
                                        SSL *ssl, bool *session_created) {
    *session_created = false;

    /* If session doesn't exist, create a new one */
    if (*session_ptr == NULL) {
        int rv = nghttp2_session_client_new(session_ptr, callbacks, stream_data);
        if (rv != 0) {
            return -1;
        }

        /* Configure optimal HTTP/2 settings for performance */
        nghttp2_settings_entry iv[3];

        /* 1. Increase initial window size to 16MB (from default 64KB)
         *    Allows more data in flight, reduces latency */
        iv[0].settings_id = NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE;
        iv[0].value = 16777216;  /* 16MB */

        /* 2. Increase max concurrent streams (from default 100)
         *    Allows more concurrent requests per connection */
        iv[1].settings_id = NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS;
        iv[1].value = 256;

        /* 3. Enable connection-level window updates
         *    Set max frame size to 16KB (default is already 16KB but be explicit) */
        iv[2].settings_id = NGHTTP2_SETTINGS_MAX_FRAME_SIZE;
        iv[2].value = 16384;

        /* Send connection preface and optimized SETTINGS */
        nghttp2_submit_settings(*session_ptr, NGHTTP2_FLAG_NONE, iv, 3);

        /* Increase connection-level flow control window to 16MB
         * This is separate from per-stream windows */
        nghttp2_submit_window_update(*session_ptr, NGHTTP2_FLAG_NONE, 0,
                                      16777216 - 65535);  /* Increase by delta */

        nghttp2_session_send(*session_ptr);

        *session_created = true;
    }

    return 0;
}

/**
 * Perform HTTP/2 request
 */
int httpmorph_http2_request(SSL *ssl, const httpmorph_request_t *request,
                             const char *host, const char *path,
                             httpmorph_response_t *response) {
    nghttp2_session *session;
    nghttp2_session_callbacks *callbacks;
    http2_stream_data_t stream_data = {0};
    int rv;

    stream_data.response = response;
    stream_data.ssl = ssl;
    stream_data.data_capacity = 16384;
    stream_data.data_buf = malloc(stream_data.data_capacity);
    if (!stream_data.data_buf) return -1;

    /* Set up request body if present */
    stream_data.req_body = (const uint8_t *)request->body;
    stream_data.req_body_len = request->body_len;
    stream_data.req_body_sent = 0;

    /* Initialize nghttp2 callbacks */
    nghttp2_session_callbacks_new(&callbacks);
    nghttp2_session_callbacks_set_send_callback(callbacks, http2_send_callback);
    nghttp2_session_callbacks_set_recv_callback(callbacks, http2_recv_callback);
    nghttp2_session_callbacks_set_on_header_callback(callbacks, http2_on_header_callback);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, http2_on_data_chunk_recv_callback);
    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, http2_on_frame_recv_callback);

    /* Create HTTP/2 client session (pass &stream_data as user_data) */
    rv = nghttp2_session_client_new(&session, callbacks, &stream_data);
    nghttp2_session_callbacks_del(callbacks);
    if (rv != 0) {
        free(stream_data.data_buf);
        return -1;
    }

    /* Configure optimal HTTP/2 settings for performance */
    nghttp2_settings_entry iv[3];

    /* 1. Increase initial window size to 16MB (from default 64KB) */
    iv[0].settings_id = NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE;
    iv[0].value = 16777216;  /* 16MB */

    /* 2. Increase max concurrent streams (from default 100) */
    iv[1].settings_id = NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS;
    iv[1].value = 256;

    /* 3. Set max frame size to 16KB */
    iv[2].settings_id = NGHTTP2_SETTINGS_MAX_FRAME_SIZE;
    iv[2].value = 16384;

    /* Send connection preface and optimized SETTINGS */
    nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, iv, 3);

    /* Increase connection-level flow control window to 16MB */
    nghttp2_submit_window_update(session, NGHTTP2_FLAG_NONE, 0,
                                  16777216 - 65535);  /* Increase by delta */

    nghttp2_session_send(session);

    /* Prepare request headers */
    nghttp2_nv hdrs[64];
    int nhdrs = 0;

    /* Add pseudo-headers first */
    const char *method_str = httpmorph_method_to_string(request->method);
    hdrs[nhdrs++] = (nghttp2_nv){(uint8_t *)":method", (uint8_t *)method_str, 7, strlen(method_str), NGHTTP2_NV_FLAG_NONE};
    hdrs[nhdrs++] = (nghttp2_nv){(uint8_t *)":path", (uint8_t *)path, 5, strlen(path), NGHTTP2_NV_FLAG_NONE};
    hdrs[nhdrs++] = (nghttp2_nv){(uint8_t *)":scheme", (uint8_t *)"https", 7, 5, NGHTTP2_NV_FLAG_NONE};
    hdrs[nhdrs++] = (nghttp2_nv){(uint8_t *)":authority", (uint8_t *)host, 10, strlen(host), NGHTTP2_NV_FLAG_NONE};

    /* Add custom headers */
    for (size_t i = 0; i < request->header_count && nhdrs < 60; i++) {
        /* Skip headers that conflict with pseudo-headers */
        if (strcasecmp(request->headers[i].key, "host") == 0) continue;

        hdrs[nhdrs++] = (nghttp2_nv){
            (uint8_t *)request->headers[i].key,
            (uint8_t *)request->headers[i].value,
            strlen(request->headers[i].key),
            strlen(request->headers[i].value),
            NGHTTP2_NV_FLAG_NONE
        };
    }

    /* Set up data provider if request has a body */
    nghttp2_data_provider data_prd;
    nghttp2_data_provider *data_prd_ptr = NULL;

    if (stream_data.req_body_len > 0) {
        data_prd.source.ptr = NULL;
        data_prd.read_callback = http2_data_source_read_callback;
        data_prd_ptr = &data_prd;
    }

    /* Set up priority spec if priority is configured */
    nghttp2_priority_spec pri_spec;
    nghttp2_priority_spec *pri_spec_ptr = NULL;

    if (request->http2_stream_dependency != 0 || request->http2_priority_weight != 16) {
        /* Priority is configured - use it */
        nghttp2_priority_spec_init(&pri_spec,
                                   request->http2_stream_dependency,
                                   request->http2_priority_weight,
                                   request->http2_priority_exclusive ? 1 : 0);
        pri_spec_ptr = &pri_spec;
    }

    /* Submit request with priority spec and data provider */
    int32_t stream_id = nghttp2_submit_request(session, pri_spec_ptr, hdrs, nhdrs, data_prd_ptr, NULL);
    if (stream_id < 0) {
        nghttp2_session_del(session);
        free(stream_data.data_buf);
        return -1;
    }

    /* Send request */
    nghttp2_session_send(session);

    /* Receive response - event loop for non-blocking I/O */
    int sockfd = SSL_get_fd(ssl);
    fd_set readfds, writefds;
    struct timeval tv;

    while (!stream_data.stream_closed &&
           (nghttp2_session_want_read(session) || nghttp2_session_want_write(session))) {

        /* Wait for socket to be ready */
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);

        if (nghttp2_session_want_read(session)) {
            FD_SET(sockfd, &readfds);
        }
        if (nghttp2_session_want_write(session)) {
            FD_SET(sockfd, &writefds);
        }

        tv.tv_sec = 5;  /* 5 second timeout */
        tv.tv_usec = 0;

        int select_rv = select(SELECT_NFDS(sockfd), &readfds, &writefds, NULL, &tv);
        if (select_rv < 0) {
            /* Error */
            rv = -1;
            break;
        } else if (select_rv == 0) {
            /* Timeout */
            rv = -1;
            break;
        }

        /* Receive data if available */
        if (FD_ISSET(sockfd, &readfds)) {
            rv = nghttp2_session_recv(session);
            if (rv != 0) {
                if (rv == NGHTTP2_ERR_EOF) {
                    /* Normal termination */
                    rv = 0;
                    break;
                }
                /* Other error */
                break;
            }
        }

        /* Send data if possible */
        if (FD_ISSET(sockfd, &writefds)) {
            rv = nghttp2_session_send(session);
            if (rv != 0) {
                break;
            }
        }
    }

    /* Set rv to 0 for success if stream closed normally */
    if (stream_data.stream_closed && rv == 0) {
        rv = 0;
    }

    /* Check for errors */
    if (rv != 0) {
        nghttp2_session_del(session);
        free(stream_data.data_buf);
        return -1;
    }

    /* Copy data to response */
    if (stream_data.data_len > 0) {
        response->body = stream_data.data_buf;
        response->body_len = stream_data.data_len;
        response->body_capacity = stream_data.data_capacity;
    } else {
        free(stream_data.data_buf);
        response->body = NULL;
        response->body_len = 0;
    }

    nghttp2_session_del(session);
    return 0;
}

/**
 * Perform HTTP/2 request with session reuse
 * Reuses nghttp2_session from pooled connection if available
 */
int httpmorph_http2_request_pooled(struct pooled_connection *conn,
                                   const httpmorph_request_t *request,
                                   const char *host, const char *path,
                                   httpmorph_response_t *response) {
    if (!conn || !conn->ssl || !request || !response) {
        return -1;
    }

    nghttp2_session *session = (nghttp2_session *)conn->http2_session;
    nghttp2_session_callbacks *callbacks;
    http2_stream_data_t stream_data = {0};
    int rv;
    bool session_created = false;

    stream_data.response = response;
    stream_data.ssl = conn->ssl;
    stream_data.data_capacity = 16384;
    stream_data.data_buf = malloc(stream_data.data_capacity);
    if (!stream_data.data_buf) return -1;

    /* Set up request body if present */
    stream_data.req_body = (const uint8_t *)request->body;
    stream_data.req_body_len = request->body_len;
    stream_data.req_body_sent = 0;

    /* If no session exists, create one */
    if (session == NULL) {
        /* Initialize nghttp2 callbacks */
        nghttp2_session_callbacks_new(&callbacks);
        nghttp2_session_callbacks_set_send_callback(callbacks, http2_send_callback);
        nghttp2_session_callbacks_set_recv_callback(callbacks, http2_recv_callback);
        nghttp2_session_callbacks_set_on_header_callback(callbacks, http2_on_header_callback);
        nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, http2_on_data_chunk_recv_callback);
        nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, http2_on_frame_recv_callback);

        /* Create session and send preface */
        rv = http2_init_or_reuse_session(&session, callbacks, &stream_data, conn->ssl, &session_created);
        nghttp2_session_callbacks_del(callbacks);

        if (rv != 0) {
            free(stream_data.data_buf);
            return -1;
        }

        /* Store session in connection for reuse */
        conn->http2_session = session;
        conn->preface_sent = true;

        /* Create and start session manager for concurrent multiplexing */
        http2_session_manager_t *mgr = http2_session_manager_create(
            session,
            callbacks,
            conn->ssl,
            conn->sockfd
        );

        if (mgr) {
            /* Start I/O thread for concurrent stream handling */
            if (http2_session_manager_start(mgr) == 0) {
                conn->http2_session_manager = mgr;
            } else {
                /* Failed to start - clean up manager */
                http2_session_manager_destroy(mgr);
                conn->http2_session_manager = NULL;
            }
        }
    }

    /* Prepare request headers */
    nghttp2_nv hdrs[64];
    int nhdrs = 0;

    /* Add pseudo-headers first */
    const char *method_str = httpmorph_method_to_string(request->method);
    hdrs[nhdrs++] = (nghttp2_nv){(uint8_t *)":method", (uint8_t *)method_str, 7, strlen(method_str), NGHTTP2_NV_FLAG_NONE};
    hdrs[nhdrs++] = (nghttp2_nv){(uint8_t *)":path", (uint8_t *)path, 5, strlen(path), NGHTTP2_NV_FLAG_NONE};
    hdrs[nhdrs++] = (nghttp2_nv){(uint8_t *)":scheme", (uint8_t *)"https", 7, 5, NGHTTP2_NV_FLAG_NONE};
    hdrs[nhdrs++] = (nghttp2_nv){(uint8_t *)":authority", (uint8_t *)host, 10, strlen(host), NGHTTP2_NV_FLAG_NONE};

    /* Add custom headers */
    for (size_t i = 0; i < request->header_count && nhdrs < 60; i++) {
        /* Skip headers that conflict with pseudo-headers */
        if (strcasecmp(request->headers[i].key, "host") == 0) continue;

        hdrs[nhdrs++] = (nghttp2_nv){
            (uint8_t *)request->headers[i].key,
            (uint8_t *)request->headers[i].value,
            strlen(request->headers[i].key),
            strlen(request->headers[i].value),
            NGHTTP2_NV_FLAG_NONE
        };
    }

    /* Set up data provider if request has a body */
    nghttp2_data_provider data_prd;
    nghttp2_data_provider *data_prd_ptr = NULL;

    if (stream_data.req_body_len > 0) {
        data_prd.source.ptr = NULL;
        data_prd.read_callback = http2_data_source_read_callback;
        data_prd_ptr = &data_prd;
    }

    /* Set up priority spec if priority is configured */
    nghttp2_priority_spec pri_spec;
    nghttp2_priority_spec *pri_spec_ptr = NULL;

    if (request->http2_stream_dependency != 0 || request->http2_priority_weight != 16) {
        /* Priority is configured - use it */
        nghttp2_priority_spec_init(&pri_spec,
                                   request->http2_stream_dependency,
                                   request->http2_priority_weight,
                                   request->http2_priority_exclusive ? 1 : 0);
        pri_spec_ptr = &pri_spec;
    }

    /* Submit request with stream-specific user data
     * Pass &stream_data as stream user data so callbacks can retrieve it per-stream.
     * This enables true HTTP/2 session reuse across multiple requests. */
    int32_t stream_id = nghttp2_submit_request(session, pri_spec_ptr, hdrs, nhdrs, data_prd_ptr, &stream_data);
    if (stream_id < 0) {
        free(stream_data.data_buf);
        return -1;
    }

    /* Send request */
    nghttp2_session_send(session);

    /* Receive response - event loop for non-blocking I/O */
    int sockfd = SSL_get_fd(conn->ssl);
    fd_set readfds, writefds;
    struct timeval tv;

    while (!stream_data.stream_closed &&
           (nghttp2_session_want_read(session) || nghttp2_session_want_write(session))) {

        /* Wait for socket to be ready */
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);

        if (nghttp2_session_want_read(session)) {
            FD_SET(sockfd, &readfds);
        }
        if (nghttp2_session_want_write(session)) {
            FD_SET(sockfd, &writefds);
        }

        tv.tv_sec = 5;  /* 5 second timeout */
        tv.tv_usec = 0;

        int select_rv = select(SELECT_NFDS(sockfd), &readfds, &writefds, NULL, &tv);
        if (select_rv < 0) {
            /* Error */
            rv = -1;
            break;
        } else if (select_rv == 0) {
            /* Timeout */
            rv = -1;
            break;
        }

        /* Receive data if available */
        if (FD_ISSET(sockfd, &readfds)) {
            rv = nghttp2_session_recv(session);
            if (rv != 0) {
                if (rv == NGHTTP2_ERR_EOF) {
                    /* Normal termination */
                    rv = 0;
                    break;
                }
                /* Other error */
                break;
            }
        }

        /* Send data if possible */
        if (FD_ISSET(sockfd, &writefds)) {
            rv = nghttp2_session_send(session);
            if (rv != 0) {
                break;
            }
        }
    }

    /* Set rv to 0 for success if stream closed normally */
    if (stream_data.stream_closed && rv == 0) {
        rv = 0;
    }

    /* Check for errors */
    if (rv != 0) {
        free(stream_data.data_buf);
        return -1;
    }

    /* Copy data to response */
    if (stream_data.data_len > 0) {
        response->body = stream_data.data_buf;
        response->body_len = stream_data.data_len;
        response->body_capacity = stream_data.data_capacity;
    } else {
        free(stream_data.data_buf);
        response->body = NULL;
        response->body_len = 0;
    }

    /* Don't delete session - keep it for reuse in the connection pool */
    return 0;
}

/**
 * Perform HTTP/2 request with concurrent multiplexing
 * Uses session manager to allow multiple concurrent streams on same session
 */
int httpmorph_http2_request_concurrent(struct pooled_connection *conn,
                                       const httpmorph_request_t *request,
                                       const char *host, const char *path,
                                       httpmorph_response_t *response) {
    if (!conn || !conn->ssl || !request || !response || !conn->http2_session_manager) {
        return -1;
    }

    http2_session_manager_t *mgr = (http2_session_manager_t*)conn->http2_session_manager;
    http2_stream_data_t *stream_data = NULL;
    int32_t stream_id = -1;
    int rv;

    /* Allocate stream data */
    stream_data = calloc(1, sizeof(http2_stream_data_t));
    if (!stream_data) {
        return -1;
    }

    stream_data->response = response;
    stream_data->ssl = conn->ssl;
    stream_data->data_capacity = 16384;
    stream_data->data_buf = malloc(stream_data->data_capacity);
    if (!stream_data->data_buf) {
        free(stream_data);
        return -1;
    }

    /* Set up request body if present */
    stream_data->req_body = (const uint8_t *)request->body;
    stream_data->req_body_len = request->body_len;
    stream_data->req_body_sent = 0;

    /* Link to session manager for callbacks */
    stream_data->session_manager = mgr;

    /* Prepare request headers */
    nghttp2_nv hdrs[64];
    int hdr_count = 0;

    /* Mandatory pseudo-headers for HTTP/2 */
    const char *method_str = httpmorph_method_to_string(request->method);
    hdrs[hdr_count].name = (uint8_t *)":method";
    hdrs[hdr_count].namelen = 7;
    hdrs[hdr_count].value = (uint8_t *)method_str;
    hdrs[hdr_count].valuelen = strlen(method_str);
    hdrs[hdr_count].flags = NGHTTP2_NV_FLAG_NONE;
    hdr_count++;

    hdrs[hdr_count].name = (uint8_t *)":path";
    hdrs[hdr_count].namelen = 5;
    hdrs[hdr_count].value = (uint8_t *)path;
    hdrs[hdr_count].valuelen = strlen(path);
    hdrs[hdr_count].flags = NGHTTP2_NV_FLAG_NONE;
    hdr_count++;

    hdrs[hdr_count].name = (uint8_t *)":scheme";
    hdrs[hdr_count].namelen = 7;
    hdrs[hdr_count].value = (uint8_t *)"https";
    hdrs[hdr_count].valuelen = 5;
    hdrs[hdr_count].flags = NGHTTP2_NV_FLAG_NONE;
    hdr_count++;

    hdrs[hdr_count].name = (uint8_t *)":authority";
    hdrs[hdr_count].namelen = 10;
    hdrs[hdr_count].value = (uint8_t *)host;
    hdrs[hdr_count].valuelen = strlen(host);
    hdrs[hdr_count].flags = NGHTTP2_NV_FLAG_NONE;
    hdr_count++;

    /* Add custom headers */
    for (size_t i = 0; i < request->header_count && hdr_count < 64; i++) {
        hdrs[hdr_count].name = (uint8_t *)request->headers[i].key;
        hdrs[hdr_count].namelen = strlen(request->headers[i].key);
        hdrs[hdr_count].value = (uint8_t *)request->headers[i].value;
        hdrs[hdr_count].valuelen = strlen(request->headers[i].value);
        hdrs[hdr_count].flags = NGHTTP2_NV_FLAG_NONE;
        hdr_count++;
    }

    /* Set up data provider for request body if needed */
    nghttp2_data_provider data_prd;
    nghttp2_data_provider *data_prd_ptr = NULL;
    if (request->body && request->body_len > 0) {
        data_prd.source.ptr = stream_data;
        data_prd.read_callback = http2_data_source_read_callback;
        data_prd_ptr = &data_prd;
    }

    /* Set up priority spec if priority is configured */
    nghttp2_priority_spec pri_spec;
    nghttp2_priority_spec *pri_spec_ptr = NULL;

    if (request->http2_stream_dependency != 0 || request->http2_priority_weight != 16) {
        /* Priority is configured - use it */
        nghttp2_priority_spec_init(&pri_spec,
                                   request->http2_stream_dependency,
                                   request->http2_priority_weight,
                                   request->http2_priority_exclusive ? 1 : 0);
        pri_spec_ptr = &pri_spec;
    }

    /* Submit stream to session manager (non-blocking) */
    rv = http2_session_manager_submit_stream(
        mgr,
        stream_data,
        pri_spec_ptr,
        hdrs,
        hdr_count,
        data_prd_ptr,
        &stream_id
    );

    if (rv != 0) {
        free(stream_data->data_buf);
        free(stream_data);
        return -1;
    }

    /* Store stream ID for later cleanup */
    stream_data->stream_id = stream_id;

    /* Wait for stream completion (blocking with timeout) */
    uint32_t timeout_ms = request->timeout_ms > 0 ? request->timeout_ms : 30000;
    rv = http2_session_manager_wait_for_stream(mgr, stream_id, timeout_ms);

    /* Copy response body to response structure */
    if (rv == 0 && stream_data->data_len > 0) {
        response->body = stream_data->data_buf;
        response->body_len = stream_data->data_len;
        response->body_capacity = stream_data->data_capacity;
        /* Transfer ownership - don't free data_buf */
    } else {
        /* Error or no body */
        free(stream_data->data_buf);
        response->body = NULL;
        response->body_len = 0;
    }

    /* Clean up stream tracking */
    http2_session_manager_remove_stream(mgr, stream_id);

    /* Free stream data structure (but not data_buf if transferred to response) */
    free(stream_data);

    return rv;
}

#endif /* HAVE_NGHTTP2 */
