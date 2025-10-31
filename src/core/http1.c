/**
 * http1.c - HTTP/1.1 protocol implementation
 */

#include "internal/http1.h"
#include "internal/util.h"
#include "internal/response.h"
#include "buffer_pool.h"
#include "request_builder.h"
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <errno.h>
#endif

/**
 * Helper: Reallocate response body buffer using buffer pool
 *
 * Returns new buffer on success, NULL on failure.
 * If successful, updates response->body, response->body_capacity, and response->_body_actual_size
 */
static uint8_t* realloc_body_buffer(httpmorph_response_t *response, size_t new_capacity) {
    uint8_t *new_body = NULL;
    size_t new_actual_size = 0;

    /* Get new buffer from pool if available */
    if (response->_buffer_pool) {
        new_body = buffer_pool_get((httpmorph_buffer_pool_t*)response->_buffer_pool,
                                   new_capacity, &new_actual_size);
    } else {
        new_body = malloc(new_capacity);
        new_actual_size = new_capacity;
    }

    if (!new_body) {
        return NULL;
    }

    /* Copy existing data to new buffer */
    if (response->body && response->body_len > 0) {
        memcpy(new_body, response->body, response->body_len);
    }

    /* Return old buffer to pool if available */
    if (response->body) {
        if (response->_buffer_pool) {
            buffer_pool_put((httpmorph_buffer_pool_t*)response->_buffer_pool,
                          response->body, response->_body_actual_size);
        } else {
            free(response->body);
        }
    }

    /* Update response structure */
    response->body = new_body;
    response->body_capacity = new_capacity;
    response->_body_actual_size = new_actual_size;

    return new_body;
}

/* Helper: Add response header (used by recv_http_response) */
static int add_response_header(httpmorph_response_t *response,
                               const char *key, const char *value) {
    /* Check if we need to grow the array - use exponential growth */
    if (response->header_count >= response->header_capacity) {
        size_t new_capacity = response->header_capacity * 2;
        httpmorph_header_t *new_headers = realloc(response->headers,
                                                   new_capacity * sizeof(httpmorph_header_t));
        if (!new_headers) {
            return -1;
        }

        response->headers = new_headers;
        response->header_capacity = new_capacity;
    }

    /* Add header at current count index */
    response->headers[response->header_count].key = strdup(key);
    response->headers[response->header_count].value = strdup(value);

    if (!response->headers[response->header_count].key ||
        !response->headers[response->header_count].value) {
        free(response->headers[response->header_count].key);
        free(response->headers[response->header_count].value);
        return -1;
    }

    response->header_count++;
    return 0;
}

/**
 * Send HTTP/1.1 request (optimized with request builder)
 */
int httpmorph_send_http_request(SSL *ssl, int sockfd, const httpmorph_request_t *request,
                                 const char *host, const char *path, const char *scheme,
                                 uint16_t port, bool use_proxy, const char *proxy_user,
                                 const char *proxy_pass) {

    /* Create request builder */
    request_builder_t *builder = request_builder_create(1024);
    if (!builder) {
        return -1;
    }

    /* Request line */
    const char *method_str = "GET";
    switch (request->method) {
        case HTTPMORPH_GET:     method_str = "GET"; break;
        case HTTPMORPH_POST:    method_str = "POST"; break;
        case HTTPMORPH_PUT:     method_str = "PUT"; break;
        case HTTPMORPH_DELETE:  method_str = "DELETE"; break;
        case HTTPMORPH_HEAD:    method_str = "HEAD"; break;
        case HTTPMORPH_OPTIONS: method_str = "OPTIONS"; break;
        case HTTPMORPH_PATCH:   method_str = "PATCH"; break;
        default: break;
    }

    /* Build request line */
    request_builder_append_str(builder, method_str);
    request_builder_append_str(builder, " ");

    /* For HTTP proxy (not HTTPS/CONNECT), use full URL in request line */
    if (use_proxy && !ssl) {
        /* HTTP proxy requires absolute URI: GET http://host:port/path HTTP/1.1 */
        request_builder_append_str(builder, scheme);
        request_builder_append_str(builder, "://");
        request_builder_append_str(builder, host);

        /* Add port if non-standard */
        if (!((strcmp(scheme, "http") == 0 && port == 80) ||
              (strcmp(scheme, "https") == 0 && port == 443))) {
            request_builder_append_str(builder, ":");
            request_builder_append_uint(builder, port);
        }
        request_builder_append_str(builder, path);
    } else {
        /* Direct connection or HTTPS through proxy (after CONNECT): use relative path */
        request_builder_append_str(builder, path);
    }

    request_builder_append_str(builder, " HTTP/1.1\r\n");

    /* Add Host header */
    size_t host_len = strlen(host);
    if ((strcmp(scheme, "http") == 0 && port != 80) || (strcmp(scheme, "https") == 0 && port != 443)) {
        /* Host with port */
        request_builder_append(builder, "Host: ", 6);
        request_builder_append(builder, host, host_len);
        request_builder_append_str(builder, ":");
        request_builder_append_uint(builder, port);
        request_builder_append_str(builder, "\r\n");
    } else {
        /* Host without port */
        request_builder_append_header(builder, "Host", 4, host, host_len);
    }

    /* Check which headers are provided */
    bool has_user_agent = false;
    bool has_accept = false;
    bool has_connection = false;
    bool has_accept_encoding = false;

    for (size_t i = 0; i < request->header_count; i++) {
        if (strcasecmp(request->headers[i].key, "User-Agent") == 0) has_user_agent = true;
        if (strcasecmp(request->headers[i].key, "Accept") == 0) has_accept = true;
        if (strcasecmp(request->headers[i].key, "Connection") == 0) has_connection = true;
        if (strcasecmp(request->headers[i].key, "Accept-Encoding") == 0) has_accept_encoding = true;
    }

    /* Add default headers if missing */
    if (!has_user_agent) {
        const char *user_agent = request->user_agent ? request->user_agent : "httpmorph/0.1.3";
        request_builder_append_header(builder, "User-Agent", 10, user_agent, strlen(user_agent));
    }
    if (!has_accept) {
        request_builder_append_header(builder, "Accept", 6, "*/*", 3);
    }
    if (!has_accept_encoding) {
        request_builder_append_header(builder, "Accept-Encoding", 15, "gzip, deflate", 13);
    }
    if (!has_connection) {
        request_builder_append_header(builder, "Connection", 10, "keep-alive", 10);
    }

    /* Add Proxy-Authorization header for HTTP proxy (not HTTPS/CONNECT) */
    if (use_proxy && !ssl && (proxy_user || proxy_pass)) {
        const char *username = proxy_user ? proxy_user : "";
        const char *password = proxy_pass ? proxy_pass : "";

        char credentials[512];
        snprintf(credentials, sizeof(credentials), "%s:%s", username, password);

        char *encoded = httpmorph_base64_encode(credentials, strlen(credentials));
        if (encoded) {
            /* Format as "Basic <base64>" */
            char auth_value[1024];
            snprintf(auth_value, sizeof(auth_value), "Basic %s", encoded);
            request_builder_append_header(builder, "Proxy-Authorization", 19, auth_value, strlen(auth_value));
            free(encoded);
        }
    }

    /* Add request headers */
    for (size_t i = 0; i < request->header_count; i++) {
        request_builder_append_header(builder,
                                      request->headers[i].key, strlen(request->headers[i].key),
                                      request->headers[i].value, strlen(request->headers[i].value));
    }

    /* Content-Length if body present */
    if (request->body && request->body_len > 0) {
        request_builder_append(builder, "Content-Length: ", 16);
        request_builder_append_uint(builder, request->body_len);
        request_builder_append_str(builder, "\r\n");
    }

    /* End of headers */
    request_builder_append_str(builder, "\r\n");

    /* Get built request */
    size_t header_len;
    const char *request_data = request_builder_data(builder, &header_len);

    /* Send request headers */
    size_t total_sent = 0;
    int result = 0;

    if (ssl) {
        while (total_sent < header_len) {
            int sent = SSL_write(ssl, request_data + total_sent, header_len - total_sent);
            if (sent <= 0) {
                result = -1;
                goto cleanup;
            }
            total_sent += sent;
        }

        /* Send body if present */
        if (request->body && request->body_len > 0) {
            total_sent = 0;
            while (total_sent < request->body_len) {
                int sent = SSL_write(ssl, request->body + total_sent,
                                   request->body_len - total_sent);
                if (sent <= 0) {
                    result = -1;
                    goto cleanup;
                }
                total_sent += sent;
            }
        }
    } else {
        while (total_sent < header_len) {
            ssize_t sent = send(sockfd, request_data + total_sent,
                               header_len - total_sent, 0);
            if (sent <= 0) {
                result = -1;
                goto cleanup;
            }
            total_sent += sent;
        }

        /* Send body if present */
        if (request->body && request->body_len > 0) {
            total_sent = 0;
            while (total_sent < request->body_len) {
                ssize_t sent = send(sockfd, request->body + total_sent,
                                   request->body_len - total_sent, 0);
                if (sent <= 0) {
                    result = -1;
                    goto cleanup;
                }
                total_sent += sent;
            }
        }
    }

cleanup:
    request_builder_destroy(builder);
    return result;
}

/**
 * Receive HTTP/1.1 response
 */
int httpmorph_recv_http_response(SSL *ssl, int sockfd, httpmorph_response_t *response,
                                  uint64_t *first_byte_time_us, bool *conn_will_close,
                                  httpmorph_method_t method) {
    char buffer[16384];
    size_t buffer_pos = 0;
    bool headers_complete = false;
    size_t content_length = 0;
    bool is_head_request = (method == HTTPMORPH_HEAD);
    bool chunked = false;
    uint64_t first_byte_time = 0;

    /* Read response headers - read in chunks, not byte by byte */
    while (!headers_complete && buffer_pos < sizeof(buffer) - 1) {
        int n;
        size_t to_read = sizeof(buffer) - buffer_pos - 1;
        if (to_read > 4096) to_read = 4096;

        if (ssl) {
            n = SSL_read(ssl, buffer + buffer_pos, to_read);
            if (n <= 0) {
                /* Check if we have any data and end of headers */
                if (buffer_pos > 0 && strstr(buffer, "\r\n\r\n")) {
                    headers_complete = true;
                    break;
                }
                /* Check for SSL timeout/errors */
                int ssl_err = SSL_get_error(ssl, n);
                if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
                    return HTTPMORPH_ERROR_TIMEOUT;
                }
                return HTTPMORPH_ERROR_NETWORK;
            }
        } else {
            n = recv(sockfd, buffer + buffer_pos, to_read, 0);
            if (n <= 0) {
                /* Check if we have any data and end of headers */
                if (buffer_pos > 0 && strstr(buffer, "\r\n\r\n")) {
                    headers_complete = true;
                    break;
                }
                /* Check errno for timeout */
#ifdef _WIN32
                int err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK || err == WSAETIMEDOUT) {
#else
                if (errno == EWOULDBLOCK || errno == EAGAIN || errno == ETIMEDOUT) {
#endif
                    return HTTPMORPH_ERROR_TIMEOUT;
                }
                return HTTPMORPH_ERROR_NETWORK;
            }
        }

        if (first_byte_time == 0) {
            first_byte_time = httpmorph_get_time_us();
        }

        buffer_pos += n;
        buffer[buffer_pos] = '\0';

        /* Check for end of headers */
        if (strstr(buffer, "\r\n\r\n")) {
            headers_complete = true;
        }
    }

    *first_byte_time_us = first_byte_time;

    /* Parse headers */
    char *headers_end = strstr(buffer, "\r\n\r\n");
    if (!headers_end) {
        return -1;
    }

    /* Save headers end position before modifying buffer */
    char *body_start = headers_end + 4;  /* Point to body data after \r\n\r\n */
    size_t body_in_buffer = buffer_pos - (body_start - buffer);

    char *line_start = buffer;
    char *line_end;
    bool first_line = true;

    /* Parse headers line by line up to headers_end */
    while (line_start < headers_end && (line_end = strstr(line_start, "\r\n")) != NULL) {
        *line_end = '\0';

        if (first_line) {
            if (httpmorph_parse_response_line(line_start, response) != 0) {
                return -1;
            }
            first_line = false;
        } else if (strlen(line_start) > 0) {
            /* Parse header: key: value */
            char *colon = strchr(line_start, ':');
            if (colon) {
                *colon = '\0';
                char *key = line_start;
                char *value = colon + 1;

                /* Skip leading whitespace in value */
                while (*value == ' ') value++;

                add_response_header(response, key, value);

                /* Check for Content-Length or Transfer-Encoding */
                if (strcasecmp(key, "Content-Length") == 0) {
                    content_length = strtoul(value, NULL, 10);
                } else if (strcasecmp(key, "Transfer-Encoding") == 0 &&
                          strstr(value, "chunked")) {
                    chunked = true;
                }
            }
        }

        line_start = line_end + 2;
    }

    /* Read response body */
    size_t body_received = 0;

    /* Copy any body data already in buffer */
    if (body_in_buffer > 0) {
        if (response->body_capacity < body_in_buffer) {
            /* Use 2x growth strategy instead of exact size */
            size_t new_capacity = body_in_buffer * 2;
            if (!realloc_body_buffer(response, new_capacity)) {
                /* Allocation failed, but continue with existing buffer */
                body_in_buffer = response->body_capacity;
            }
        }
        memcpy(response->body, body_start, body_in_buffer);
        body_received = body_in_buffer;
    }

    /* For HEAD requests, never read body even if Content-Length is present */
    if (is_head_request) {
        response->body_len = 0;
        if (first_byte_time_us) {
            *first_byte_time_us = first_byte_time;
        }
        return 0;
    }

    /* Allocate body buffer and read based on Content-Length if known */
    if (content_length > 0 && content_length < 100 * 1024 * 1024) {
        /* Known content length - pre-allocate exact size */
        if (response->body_capacity < content_length) {
            if (!realloc_body_buffer(response, content_length)) {
                /* Allocation failed - will read what fits */
            }
        }

        while (body_received < content_length) {
            int n;
            if (ssl) {
                n = SSL_read(ssl, response->body + body_received,
                           content_length - body_received);
            } else {
                n = recv(sockfd, response->body + body_received,
                        content_length - body_received, 0);
            }

            if (n <= 0) break;
            body_received += n;
        }
    } else if (chunked) {
        /* Chunked transfer encoding - parse chunk sizes and data */
        /* Format: [size-hex]\r\n[data]\r\n ... 0\r\n\r\n */

        char chunk_buffer[16384];
        size_t chunk_buffer_pos = 0;

        /* Copy any body data already in buffer to chunk buffer */
        if (body_in_buffer > 0 && body_in_buffer < sizeof(chunk_buffer)) {
            memcpy(chunk_buffer, body_start, body_in_buffer);
            chunk_buffer_pos = body_in_buffer;
            body_received = 0;  /* Reset, we'll parse chunks properly */
        }

        bool last_chunk = false;
        while (!last_chunk) {
            /* Read chunk size line */
            char *chunk_size_end = NULL;

            /* Read more data if we don't have a complete chunk size line */
            while (!chunk_size_end && chunk_buffer_pos < sizeof(chunk_buffer) - 1) {
                /* Look for \r\n marking end of chunk size */
                chunk_buffer[chunk_buffer_pos] = '\0';
                chunk_size_end = strstr(chunk_buffer, "\r\n");
                if (chunk_size_end) break;

                /* Read more data */
                int n;
                if (ssl) {
                    n = SSL_read(ssl, chunk_buffer + chunk_buffer_pos,
                               sizeof(chunk_buffer) - chunk_buffer_pos - 1);
                } else {
                    n = recv(sockfd, chunk_buffer + chunk_buffer_pos,
                           sizeof(chunk_buffer) - chunk_buffer_pos - 1, 0);
                }

                if (n <= 0) {
                    /* Connection closed or error - treat what we have as complete */
                    last_chunk = true;
                    break;
                }
                chunk_buffer_pos += n;
            }

            if (last_chunk && !chunk_size_end) break;
            if (!chunk_size_end) break;  /* Buffer full without finding size */

            /* Parse chunk size (hex) */
            size_t chunk_size = strtoul(chunk_buffer, NULL, 16);

            /* Check for last chunk (size 0) */
            if (chunk_size == 0) {
                last_chunk = true;
                /* Read trailing \r\n after 0\r\n */
                break;
            }

            /* Move past chunk size line */
            size_t chunk_size_line_len = (chunk_size_end - chunk_buffer) + 2;  /* Include \r\n */
            size_t data_in_buffer = chunk_buffer_pos - chunk_size_line_len;
            memmove(chunk_buffer, chunk_size_end + 2, data_in_buffer);
            chunk_buffer_pos = data_in_buffer;

            /* Read chunk data */
            size_t chunk_received = 0;
            while (chunk_received < chunk_size + 2) {  /* +2 for trailing \r\n */
                /* Use data from chunk_buffer first */
                if (chunk_buffer_pos > 0) {
                    size_t to_copy = chunk_buffer_pos;
                    if (chunk_received + to_copy > chunk_size + 2) {
                        to_copy = chunk_size + 2 - chunk_received;
                    }

                    /* Only copy actual chunk data, not trailing \r\n */
                    if (chunk_received < chunk_size) {
                        size_t data_to_copy = to_copy;
                        if (chunk_received + data_to_copy > chunk_size) {
                            data_to_copy = chunk_size - chunk_received;
                        }

                        /* Ensure response body has enough space BEFORE copying */
                        if (body_received + data_to_copy > response->body_capacity) {
                            size_t new_capacity = (body_received + data_to_copy) * 2;
                            if (!realloc_body_buffer(response, new_capacity)) {
                                last_chunk = true;
                                break;
                            }
                        }

                        memcpy(response->body + body_received, chunk_buffer, data_to_copy);
                        body_received += data_to_copy;
                    }

                    chunk_received += to_copy;
                    memmove(chunk_buffer, chunk_buffer + to_copy, chunk_buffer_pos - to_copy);
                    chunk_buffer_pos -= to_copy;
                    continue;
                }

                /* Read more data */
                int n;
                if (ssl) {
                    n = SSL_read(ssl, chunk_buffer, sizeof(chunk_buffer));
                } else {
                    n = recv(sockfd, chunk_buffer, sizeof(chunk_buffer), 0);
                }

                if (n <= 0) {
                    last_chunk = true;
                    break;
                }
                chunk_buffer_pos = n;
            }
        }
    } else {
        /* No content length - read until EOF */
        if (conn_will_close) *conn_will_close = true;
        while (body_received < response->body_capacity - 1024) {
            int n;
            if (ssl) {
                n = SSL_read(ssl, response->body + body_received,
                           response->body_capacity - body_received - 1);
            } else {
                n = recv(sockfd, response->body + body_received,
                        response->body_capacity - body_received - 1, 0);
            }

            if (n <= 0) break;
            body_received += n;

            /* Expand buffer if needed */
            if (body_received > response->body_capacity - 2048) {
                size_t new_capacity = response->body_capacity * 2;
                if (!realloc_body_buffer(response, new_capacity)) break;
            }
        }
    }

    response->body_len = body_received;
    return 0;
}
