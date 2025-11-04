/**
 * core.h - Core orchestration for HTTP requests
 */

#ifndef CORE_H
#define CORE_H

#include "internal.h"
#include "client.h"
#include "request.h"
#include "response.h"
#include "../connection_pool.h"

/**
 * Execute an HTTP request (main orchestration function)
 *
 * This function coordinates all HTTP request operations:
 * 1. URL parsing
 * 2. TCP connection (direct or via proxy)
 * 3. TLS handshake (if HTTPS)
 * 4. HTTP/2 or HTTP/1.1 request/response
 * 5. Gzip decompression
 * 6. Connection pooling
 *
 * @param client HTTP client with SSL context and configuration
 * @param request Request to execute
 * @param pool Connection pool for keep-alive (can be NULL)
 * @return Response object (always non-NULL, check response->error for errors)
 */
httpmorph_response_t* httpmorph_request_execute(
    httpmorph_client_t *client,
    const httpmorph_request_t *request,
    httpmorph_pool_t *pool);

#endif /* CORE_H */
