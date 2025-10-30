/**
 * request.c - HTTP request structures and operations
 */

#include "internal/request.h"
#include "string_intern.h"

/* Initial header capacity */
#define INITIAL_HEADER_CAPACITY 16

/**
 * Convert HTTP method enum to string
 */
const char* httpmorph_method_to_string(httpmorph_method_t method) {
    switch (method) {
        case HTTPMORPH_GET: return "GET";
        case HTTPMORPH_POST: return "POST";
        case HTTPMORPH_PUT: return "PUT";
        case HTTPMORPH_DELETE: return "DELETE";
        case HTTPMORPH_HEAD: return "HEAD";
        case HTTPMORPH_OPTIONS: return "OPTIONS";
        case HTTPMORPH_PATCH: return "PATCH";
        default: return "GET";
    }
}

/**
 * Create a new request
 */
httpmorph_request_t* httpmorph_request_create(httpmorph_method_t method,
                                              const char *url) {
    if (!url) {
        return NULL;
    }

    httpmorph_request_t *request = calloc(1, sizeof(httpmorph_request_t));
    if (!request) {
        return NULL;
    }

    request->method = method;
    request->url = strdup(url);
    request->timeout_ms = 30000;  /* Default 30 seconds */
    request->http_version = HTTPMORPH_VERSION_1_1;

    /* HTTP/2 priority defaults (RFC 7540 Section 5.3.5) */
    request->http2_stream_dependency = 0;      /* No dependency */
    request->http2_priority_weight = 16;       /* Default weight (middle priority) */
    request->http2_priority_exclusive = false; /* Non-exclusive */

    /* TLS configuration defaults */
    request->verify_ssl = true;        /* Verify SSL certificates by default */
    request->min_tls_version = 0;      /* Use library default (TLS 1.2+) */
    request->max_tls_version = 0;      /* Use library default (TLS 1.3) */

    /* Pre-allocate headers array for better cache locality */
    request->header_capacity = INITIAL_HEADER_CAPACITY;
    request->headers = (httpmorph_header_t*)malloc(request->header_capacity * sizeof(httpmorph_header_t));
    if (!request->headers) {
        free(request->url);
        free(request);
        return NULL;
    }

    return request;
}

/**
 * Destroy a request
 */
void httpmorph_request_destroy(httpmorph_request_t *request) {
    if (!request) {
        return;
    }

    free(request->url);
    free(request->host);
    free(request->browser_version);
    free(request->proxy_url);
    free(request->ja3_string);
    free(request->user_agent);

    /* Free headers */
    for (size_t i = 0; i < request->header_count; i++) {
        /* Only free key if it's not an interned string */
        if (!string_intern_is_interned(request->headers[i].key)) {
            free(request->headers[i].key);
        }
        free(request->headers[i].value);
    }
    /* Free the headers array */
    free(request->headers);

    /* Free body */
    free(request->body);

    free(request);
}

/**
 * Add header to request
 */
int httpmorph_request_add_header(httpmorph_request_t *request,
                                 const char *key, const char *value) {
    if (!request || !key || !value) {
        return -1;
    }

    /* Check if we need to grow the array - use exponential growth */
    if (request->header_count >= request->header_capacity) {
        size_t new_capacity = request->header_capacity * 2;
        httpmorph_header_t *new_headers = (httpmorph_header_t*)realloc(request->headers,
                                                                        new_capacity * sizeof(httpmorph_header_t));
        if (!new_headers) {
            return -1;
        }

        request->headers = new_headers;
        request->header_capacity = new_capacity;
    }

    /* Try to intern the header name first (reduces memory usage) */
    size_t key_len = strlen(key);
    const char *interned_key = string_intern_get(key, key_len);
    if (interned_key) {
        /* Use interned string (no allocation needed) */
        request->headers[request->header_count].key = (char*)interned_key;
    } else {
        /* Not a common header, allocate normally */
        request->headers[request->header_count].key = strdup(key);
        if (!request->headers[request->header_count].key) {
            return -1;
        }
    }

    /* Always allocate value (values are unique) */
    request->headers[request->header_count].value = strdup(value);
    if (!request->headers[request->header_count].value) {
        if (!interned_key) {
            free(request->headers[request->header_count].key);
        }
        return -1;
    }

    request->header_count++;
    return 0;
}

/**
 * Set request body
 */
int httpmorph_request_set_body(httpmorph_request_t *request,
                               const uint8_t *body, size_t body_len) {
    if (!request || !body) {
        return -1;
    }

    /* Free existing body */
    free(request->body);

    /* Allocate new body */
    request->body = malloc(body_len);
    if (!request->body) {
        return -1;
    }

    memcpy(request->body, body, body_len);
    request->body_len = body_len;

    return 0;
}

/**
 * Set request timeout in milliseconds
 */
void httpmorph_request_set_timeout(httpmorph_request_t *request,
                                   uint32_t timeout_ms) {
    if (request) {
        request->timeout_ms = timeout_ms;
    }
}

/**
 * Set proxy configuration
 */
void httpmorph_request_set_proxy(httpmorph_request_t *request,
                                 const char *proxy_url,
                                 const char *username,
                                 const char *password) {
    if (!request) return;

    if (request->proxy_url) {
        free(request->proxy_url);
        request->proxy_url = NULL;
    }
    if (request->proxy_username) {
        free(request->proxy_username);
        request->proxy_username = NULL;
    }
    if (request->proxy_password) {
        free(request->proxy_password);
        request->proxy_password = NULL;
    }

    if (proxy_url) {
        request->proxy_url = strdup(proxy_url);
    }
    if (username) {
        request->proxy_username = strdup(username);
    }
    if (password) {
        request->proxy_password = strdup(password);
    }
}

/**
 * Set HTTP/2 enabled flag
 */
void httpmorph_request_set_http2(httpmorph_request_t *request, bool enabled) {
    if (request) {
        request->http2_enabled = enabled;
    }
}

/**
 * Set HTTP/2 priority for request
 *
 * @param request Request to configure
 * @param stream_dependency Parent stream ID (0 for no dependency)
 * @param weight Priority weight: 1-256 (higher = more important, default: 16)
 * @param exclusive Whether to make this stream an exclusive child of parent
 */
void httpmorph_request_set_http2_priority(httpmorph_request_t *request,
                                          int32_t stream_dependency,
                                          int32_t weight,
                                          bool exclusive) {
    if (!request) {
        return;
    }

    /* Clamp weight to valid range (1-256) */
    if (weight < 1) weight = 1;
    if (weight > 256) weight = 256;

    request->http2_stream_dependency = stream_dependency;
    request->http2_priority_weight = weight;
    request->http2_priority_exclusive = exclusive;
}

/**
 * Set SSL verification mode for request
 *
 * @param request Request to configure
 * @param verify Whether to verify SSL certificates
 */
void httpmorph_request_set_verify_ssl(httpmorph_request_t *request, bool verify) {
    if (request) {
        request->verify_ssl = verify;
    }
}

/**
 * Set TLS version range for request
 *
 * @param request Request to configure
 * @param min_version Minimum TLS version (0 for default)
 * @param max_version Maximum TLS version (0 for default)
 */
void httpmorph_request_set_tls_version(httpmorph_request_t *request,
                                       uint16_t min_version,
                                       uint16_t max_version) {
    if (request) {
        request->min_tls_version = min_version;
        request->max_tls_version = max_version;
    }
}
