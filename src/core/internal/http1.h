/**
 * http1.h - HTTP/1.1 protocol implementation
 */

#ifndef HTTP1_H
#define HTTP1_H

#include "internal.h"
#include "request.h"
#include "response.h"
#include <openssl/ssl.h>

/**
 * Send HTTP/1.1 request over a connection
 *
 * @param ssl SSL connection (NULL if not using TLS)
 * @param sockfd Socket file descriptor
 * @param request Request object
 * @param host Target host
 * @param path URL path
 * @param scheme URL scheme ("http" or "https")
 * @param port Target port
 * @param use_proxy Whether connection is via proxy
 * @param proxy_user Proxy username (NULL if not using auth)
 * @param proxy_pass Proxy password (NULL if not using auth)
 * @return 0 on success, -1 on error
 */
int httpmorph_send_http_request(SSL *ssl, int sockfd, const httpmorph_request_t *request,
                                 const char *host, const char *path, const char *scheme,
                                 uint16_t port, bool use_proxy, const char *proxy_user,
                                 const char *proxy_pass);

/**
 * Receive HTTP/1.1 response from a connection
 *
 * @param ssl SSL connection (NULL if not using TLS)
 * @param sockfd Socket file descriptor
 * @param response Response object to populate
 * @param first_byte_time_us Output: time of first byte received (microseconds)
 * @param conn_will_close Output: whether connection should be closed
 * @param method HTTP method used in request (needed for HEAD handling)
 * @return 0 on success, error code on failure
 */
int httpmorph_recv_http_response(SSL *ssl, int sockfd, httpmorph_response_t *response,
                                  uint64_t *first_byte_time_us, bool *conn_will_close,
                                  httpmorph_method_t method);

#endif /* HTTP1_H */
