/**
 * network.h - TCP connection and socket operations
 */

#ifndef NETWORK_H
#define NETWORK_H

#include "internal.h"

/**
 * Establish a TCP connection to a host
 *
 * @param host Hostname or IP address
 * @param port Port number
 * @param timeout_ms Connection timeout in milliseconds
 * @param connect_time Output: connection time in microseconds
 * @return socket file descriptor on success, -1 on error
 */
int httpmorph_tcp_connect(const char *host, uint16_t port, uint32_t timeout_ms,
                          uint64_t *connect_time);

/**
 * Lookup hostname in DNS cache
 * Returns cached addrinfo if found and not expired, NULL otherwise
 * Caller must free the result with dns_cache_free_result()
 */
struct addrinfo* dns_cache_lookup(const char *hostname, uint16_t port);

/**
 * Add entry to DNS cache
 */
void dns_cache_add(const char *hostname, uint16_t port, const struct addrinfo *result);

/**
 * Free a DNS cache lookup result
 */
void dns_cache_free_result(struct addrinfo *result);

/**
 * Cleanup expired DNS cache entries
 */
void dns_cache_cleanup(void);

/**
 * Clear all DNS cache entries
 */
void dns_cache_clear(void);

#endif /* NETWORK_H */
