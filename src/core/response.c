/**
 * response.c - HTTP response structures and operations
 */

#include "internal/response.h"
#include "buffer_pool.h"
#include "string_intern.h"

/* Initial header capacity */
#define INITIAL_HEADER_CAPACITY 32

/**
 * Create a new response structure
 */
httpmorph_response_t* httpmorph_response_create(httpmorph_buffer_pool_t *buffer_pool) {
    httpmorph_response_t *resp = calloc(1, sizeof(httpmorph_response_t));
    if (!resp) {
        return NULL;
    }

    /* Pre-allocate headers array for better cache locality */
    resp->header_capacity = INITIAL_HEADER_CAPACITY;
    resp->headers = (httpmorph_header_t*)malloc(resp->header_capacity * sizeof(httpmorph_header_t));
    if (!resp->headers) {
        free(resp);
        return NULL;
    }

    /* Store buffer pool for later use */
    resp->_buffer_pool = buffer_pool;

    /* Allocate body buffer from pool if available, otherwise use malloc */
    resp->body_capacity = 65536;  /* 64KB initial - reduced reallocations for typical responses */
    if (buffer_pool) {
        resp->body = buffer_pool_get(buffer_pool, resp->body_capacity, &resp->_body_actual_size);
    } else {
        resp->body = malloc(resp->body_capacity);
        resp->_body_actual_size = resp->body_capacity;
    }

    if (!resp->body) {
        free(resp->headers);
        free(resp);
        return NULL;
    }

    return resp;
}

/**
 * Destroy a response
 */
void httpmorph_response_destroy(httpmorph_response_t *response) {
    if (!response) {
        return;
    }

    /* Free headers */
    for (size_t i = 0; i < response->header_count; i++) {
        /* Only free key if it's not an interned string */
        if (!string_intern_is_interned(response->headers[i].key)) {
            free(response->headers[i].key);
        }
        free(response->headers[i].value);
    }
    /* Free the headers array */
    free(response->headers);

    /* Return body buffer to pool if available, otherwise free */
    if (response->body) {
        if (response->_buffer_pool) {
            buffer_pool_put((httpmorph_buffer_pool_t*)response->_buffer_pool,
                          response->body, response->_body_actual_size);
        } else {
            free(response->body);
        }
    }

    /* Free TLS info */
    free(response->tls_version);
    free(response->tls_cipher);
    free(response->ja3_fingerprint);

    /* Free error message */
    free(response->error_message);

    free(response);
}

/**
 * Parse HTTP response status line
 */
int httpmorph_parse_response_line(const char *line, httpmorph_response_t *response) {
    /* Parse: HTTP/1.1 200 OK */
    int major, minor, status;
    if (sscanf(line, "HTTP/%d.%d %d", &major, &minor, &status) == 3) {
        response->status_code = status;
        if (major == 1 && minor == 1) {
            response->http_version = HTTPMORPH_VERSION_1_1;
        } else if (major == 1 && minor == 0) {
            response->http_version = HTTPMORPH_VERSION_1_0;
        } else if (major == 2) {
            response->http_version = HTTPMORPH_VERSION_2_0;
        }
        return 0;
    }
    return -1;
}

/**
 * Add a header to response (with length for HTTP/2)
 */
int httpmorph_response_add_header_internal(httpmorph_response_t *response,
                                            const char *name, size_t namelen,
                                            const char *value, size_t valuelen) {
    /* Skip pseudo-headers for HTTP/2 */
    if (namelen > 0 && name[0] == ':') {
        return 0;
    }

    /* Check if we need to grow the array - use exponential growth */
    if (response->header_count >= response->header_capacity) {
        size_t new_capacity = response->header_capacity * 2;
        httpmorph_header_t *new_headers = (httpmorph_header_t*)realloc(response->headers,
                                                                        new_capacity * sizeof(httpmorph_header_t));
        if (!new_headers) {
            return -1;
        }

        response->headers = new_headers;
        response->header_capacity = new_capacity;
    }

    /* Try to intern the header name first (reduces memory usage) */
    const char *interned_key = string_intern_get(name, namelen);
    if (interned_key) {
        /* Use interned string (no allocation needed) */
        response->headers[response->header_count].key = (char*)interned_key;
    } else {
        /* Not a common header, allocate normally */
        response->headers[response->header_count].key = strndup(name, namelen);
        if (!response->headers[response->header_count].key) {
            return -1;
        }
    }

    /* Always allocate value (values are unique) */
    response->headers[response->header_count].value = strndup(value, valuelen);
    if (!response->headers[response->header_count].value) {
        if (!interned_key) {
            free(response->headers[response->header_count].key);
        }
        return -1;
    }

    response->header_count++;
    return 0;
}

/**
 * Get response header value by key
 */
const char* httpmorph_response_get_header(const httpmorph_response_t *response,
                                           const char *key) {
    if (!response || !key) {
        return NULL;
    }

    for (size_t i = 0; i < response->header_count; i++) {
        if (strcasecmp(response->headers[i].key, key) == 0) {
            return response->headers[i].value;
        }
    }

    return NULL;
}
