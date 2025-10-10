/**
 * http2_client.c - HTTP/2 client implementation
 * Stub implementation
 */

#include "http2_client.h"
#include <stdlib.h>
#include <string.h>

/* Stub implementations for HTTP/2 client */

http2_response_t *http2_get(const char *url) {
    (void)url;  /* Unused parameter */
    /* Return NULL to indicate not implemented */
    return NULL;
}

void http2_response_free(http2_response_t *response) {
    if (!response) {
        return;
    }
    if (response->body) {
        free(response->body);
    }
    free(response);
}
