/**
 * proxy.h - HTTP proxy handling
 */

#ifndef PROXY_H
#define PROXY_H

#include "internal.h"

/**
 * Parse a proxy URL
 *
 * @param proxy_url Proxy URL (e.g., "http://user:pass@proxy.com:8080")
 * @param host Output: proxy hostname - caller must free
 * @param port Output: proxy port
 * @param username Output: proxy username (NULL if not present) - caller must free
 * @param password Output: proxy password (NULL if not present) - caller must free
 * @param use_tls Output: whether proxy connection should use TLS
 * @return 0 on success, -1 on error
 */
int httpmorph_parse_proxy_url(const char *proxy_url, char **host, uint16_t *port,
                               char **username, char **password, bool *use_tls);

/**
 * Send HTTP CONNECT request to establish tunnel through proxy
 *
 * @param sockfd Socket connected to proxy
 * @param proxy_ssl SSL connection to proxy (NULL if not using TLS to proxy)
 * @param target_host Target hostname
 * @param target_port Target port
 * @param proxy_username Proxy username (NULL if not required)
 * @param proxy_password Proxy password (NULL if not required)
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, -1 on error
 */
int httpmorph_proxy_connect(int sockfd, SSL *proxy_ssl, const char *target_host,
                             uint16_t target_port, const char *proxy_username,
                             const char *proxy_password, uint32_t timeout_ms);

#endif /* PROXY_H */
