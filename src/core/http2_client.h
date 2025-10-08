/**
 * http2_client.h - Minimal HTTP/2 client interface
 */

#ifndef HTTP2_CLIENT_H
#define HTTP2_CLIENT_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    int status_code;
    uint8_t *body;
    size_t body_len;
    int http_version;  /* 2 for HTTP/2 */
} http2_response_t;

/**
 * Execute a simple HTTP/2 GET request
 * Returns NULL on error
 */
http2_response_t *http2_get(const char *url);

/**
 * Free response structure
 */
void http2_response_free(http2_response_t *response);

#endif /* HTTP2_CLIENT_H */
