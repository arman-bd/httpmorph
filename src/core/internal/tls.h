/**
 * tls.h - TLS/SSL operations and fingerprinting
 */

#ifndef TLS_H
#define TLS_H

#include "internal.h"

/**
 * Configure SSL context with browser profile
 *
 * @param ctx SSL context to configure
 * @param profile Browser profile for fingerprinting
 * @return 0 on success, -1 on error
 */
int httpmorph_configure_ssl_ctx(SSL_CTX *ctx, const browser_profile_t *profile);

/**
 * Configure SSL context TLS version range
 *
 * @param ctx SSL context to configure
 * @param min_version Minimum TLS version (0 for default)
 * @param max_version Maximum TLS version (0 for default)
 * @return 0 on success, -1 on error
 */
int httpmorph_set_tls_version_range(SSL_CTX *ctx, uint16_t min_version, uint16_t max_version);

/**
 * Configure SSL verification mode
 *
 * @param ctx SSL context to configure
 * @param verify Whether to verify SSL certificates
 * @return 0 on success, -1 on error
 */
int httpmorph_set_ssl_verification(SSL_CTX *ctx, bool verify);

/**
 * Establish TLS connection on existing socket
 *
 * @param ctx SSL context
 * @param sockfd Socket file descriptor
 * @param hostname Hostname for SNI
 * @param browser_profile Browser profile for fingerprinting
 * @param http2_enabled Whether HTTP/2 is enabled
 * @param verify_cert Whether to verify server certificate
 * @param tls_time Output: TLS handshake time in microseconds
 * @return SSL* on success, NULL on error
 */
SSL* httpmorph_tls_connect(SSL_CTX *ctx, int sockfd, const char *hostname,
                            const browser_profile_t *browser_profile,
                            bool http2_enabled, bool verify_cert, uint64_t *tls_time);

/**
 * Calculate JA3 fingerprint from SSL connection
 *
 * @param ssl SSL connection
 * @param profile Browser profile used
 * @return JA3 fingerprint string (caller must free) or NULL on error
 */
char* httpmorph_calculate_ja3(SSL *ssl, const browser_profile_t *profile);

#ifdef _WIN32
/**
 * Load CA certificates from Windows Certificate Store into SSL_CTX
 *
 * @param ctx SSL context to load certificates into
 * @return 0 on success, -1 on error
 */
int httpmorph_load_windows_ca_certs(SSL_CTX *ctx);
#endif

#endif /* TLS_H */
