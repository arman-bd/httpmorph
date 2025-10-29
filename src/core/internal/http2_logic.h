/**
 * http2_logic.h - HTTP/2 protocol implementation
 */

#ifndef HTTP2_LOGIC_H
#define HTTP2_LOGIC_H

#include "internal.h"
#include "request.h"
#include "response.h"

#ifdef HAVE_NGHTTP2
#include <nghttp2/nghttp2.h>
#include <openssl/ssl.h>

/**
 * Perform HTTP/2 request over an existing TLS connection
 *
 * @param ssl SSL connection (must be established with HTTP/2 ALPN)
 * @param request Request object
 * @param host Target host (for :authority pseudo-header)
 * @param path URL path (for :path pseudo-header)
 * @param response Response object to populate
 * @return 0 on success, -1 on error
 */
int httpmorph_http2_request(SSL *ssl, const httpmorph_request_t *request,
                             const char *host, const char *path,
                             httpmorph_response_t *response);

/**
 * Perform HTTP/2 request with session reuse
 * Reuses nghttp2_session from pooled connection if available
 *
 * @param conn Pooled connection (may have existing HTTP/2 session)
 * @param request Request object
 * @param host Target host (for :authority pseudo-header)
 * @param path URL path (for :path pseudo-header)
 * @param response Response object to populate
 * @return 0 on success, -1 on error
 */
int httpmorph_http2_request_pooled(struct pooled_connection *conn,
                                   const httpmorph_request_t *request,
                                   const char *host, const char *path,
                                   httpmorph_response_t *response);

/**
 * Perform HTTP/2 request with concurrent multiplexing
 * Uses session manager to allow multiple concurrent streams on same session
 * This is the high-performance version for async/concurrent workloads
 *
 * @param conn Pooled connection with session manager
 * @param request Request object
 * @param host Target host (for :authority pseudo-header)
 * @param path URL path (for :path pseudo-header)
 * @param response Response object to populate
 * @return 0 on success, -1 on error
 */
int httpmorph_http2_request_concurrent(struct pooled_connection *conn,
                                       const httpmorph_request_t *request,
                                       const char *host, const char *path,
                                       httpmorph_response_t *response);

#endif /* HAVE_NGHTTP2 */

#endif /* HTTP2_LOGIC_H */
