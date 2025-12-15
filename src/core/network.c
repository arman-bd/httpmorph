/**
 * network.c - TCP connection and socket operations
 */

#include "internal/network.h"
#include "internal/util.h"

#ifndef _WIN32
#include <pthread.h>
#endif

/* ====================================================================
 * DNS CACHING
 * ==================================================================== */

#define DNS_CACHE_TTL_SECONDS 300  /* 5 minutes */
#define DNS_CACHE_MAX_ENTRIES 128

/**
 * DNS cache entry structure
 */
typedef struct dns_cache_entry {
    char *hostname;
    uint16_t port;
    struct addrinfo *result;   /* Cached addrinfo result */
    time_t expires;            /* Expiration timestamp */
    struct dns_cache_entry *next;
} dns_cache_entry_t;

/* Global DNS cache */
static dns_cache_entry_t *dns_cache_head = NULL;
static size_t dns_cache_size = 0;

#ifdef _WIN32
static CRITICAL_SECTION dns_cache_mutex;
static bool dns_cache_mutex_initialized = false;
#else
static pthread_mutex_t dns_cache_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

/**
 * Initialize DNS cache mutex (called on first use)
 */
static void dns_cache_init_mutex(void) {
#ifdef _WIN32
    if (!dns_cache_mutex_initialized) {
        InitializeCriticalSection(&dns_cache_mutex);
        dns_cache_mutex_initialized = true;
    }
#endif
}

/**
 * Lock DNS cache mutex
 */
static inline void dns_cache_lock(void) {
#ifdef _WIN32
    EnterCriticalSection(&dns_cache_mutex);
#else
    pthread_mutex_lock(&dns_cache_mutex);
#endif
}

/**
 * Unlock DNS cache mutex
 */
static inline void dns_cache_unlock(void) {
#ifdef _WIN32
    LeaveCriticalSection(&dns_cache_mutex);
#else
    pthread_mutex_unlock(&dns_cache_mutex);
#endif
}

/**
 * Deep copy addrinfo structure (needed for caching)
 */
static struct addrinfo* addrinfo_deep_copy(const struct addrinfo *src) {
    if (!src) return NULL;

    struct addrinfo *copy = (struct addrinfo*)malloc(sizeof(struct addrinfo));
    if (!copy) return NULL;

    memcpy(copy, src, sizeof(struct addrinfo));
    copy->ai_addr = NULL;
    copy->ai_canonname = NULL;
    copy->ai_next = NULL;

    /* Copy sockaddr */
    if (src->ai_addr) {
        copy->ai_addr = (struct sockaddr*)malloc(src->ai_addrlen);
        if (!copy->ai_addr) {
            /* Allocation failed - cleanup and return NULL */
            free(copy);
            return NULL;
        }
        memcpy(copy->ai_addr, src->ai_addr, src->ai_addrlen);
    }

    /* Copy canonname */
    if (src->ai_canonname) {
        copy->ai_canonname = strdup(src->ai_canonname);
        if (!copy->ai_canonname) {
            /* Allocation failed - cleanup and return NULL */
            free(copy->ai_addr);
            free(copy);
            return NULL;
        }
    }

    /* Recursively copy linked list */
    if (src->ai_next) {
        copy->ai_next = addrinfo_deep_copy(src->ai_next);
        if (!copy->ai_next && src->ai_next) {
            /* Recursive copy failed - cleanup and return NULL */
            free(copy->ai_canonname);
            free(copy->ai_addr);
            free(copy);
            return NULL;
        }
    }

    return copy;
}

/**
 * Free addrinfo deep copy
 */
static void addrinfo_deep_free(struct addrinfo *ai) {
    while (ai) {
        struct addrinfo *next = ai->ai_next;
        free(ai->ai_addr);
        free(ai->ai_canonname);
        free(ai);
        ai = next;
    }
}

/**
 * Free a DNS cache lookup result (deep copied addrinfo)
 */
void dns_cache_free_result(struct addrinfo *result) {
    addrinfo_deep_free(result);
}

/**
 * Lookup hostname in DNS cache
 * Returns cached addrinfo if found and not expired, NULL otherwise
 */
struct addrinfo* dns_cache_lookup(const char *hostname, uint16_t port) {
    if (!hostname) return NULL;

    dns_cache_init_mutex();
    dns_cache_lock();

    time_t now = time(NULL);
    dns_cache_entry_t *entry = dns_cache_head;

    while (entry) {
        if (entry->port == port &&
            strcmp(entry->hostname, hostname) == 0) {

            /* Check if entry is expired */
            if (now >= entry->expires) {
                /* Expired - will be cleaned up later */
                dns_cache_unlock();
                return NULL;
            }

            /* Found valid entry - deep copy and return */
            struct addrinfo *result = addrinfo_deep_copy(entry->result);
            dns_cache_unlock();
            return result;
        }
        entry = entry->next;
    }

    dns_cache_unlock();
    return NULL;
}

/**
 * Add entry to DNS cache
 */
void dns_cache_add(const char *hostname, uint16_t port,
                   const struct addrinfo *result) {
    if (!hostname || !result) return;

    dns_cache_init_mutex();
    dns_cache_lock();

    /* Check if cache is full - remove oldest entry if needed */
    if (dns_cache_size >= DNS_CACHE_MAX_ENTRIES) {
        /* Simple eviction: remove last entry */
        dns_cache_entry_t *prev = NULL;
        dns_cache_entry_t *curr = dns_cache_head;
        dns_cache_entry_t *last_prev = NULL;
        dns_cache_entry_t *last = NULL;

        while (curr) {
            if (!curr->next) {
                last = curr;
                last_prev = prev;
                break;
            }
            prev = curr;
            curr = curr->next;
        }

        if (last) {
            if (last_prev) {
                last_prev->next = NULL;
            } else {
                dns_cache_head = NULL;
            }

            free(last->hostname);
            addrinfo_deep_free(last->result);
            free(last);
            dns_cache_size--;
        }
    }

    /* Create new entry */
    dns_cache_entry_t *entry = (dns_cache_entry_t*)calloc(1, sizeof(dns_cache_entry_t));
    if (!entry) {
        dns_cache_unlock();
        return;
    }

    /* Allocate hostname with error checking */
    entry->hostname = strdup(hostname);
    if (!entry->hostname) {
        free(entry);
        dns_cache_unlock();
        return;
    }

    /* Deep copy addrinfo with error checking */
    entry->result = addrinfo_deep_copy(result);
    if (!entry->result) {
        free(entry->hostname);
        free(entry);
        dns_cache_unlock();
        return;
    }

    entry->port = port;
    entry->expires = time(NULL) + DNS_CACHE_TTL_SECONDS;
    entry->next = dns_cache_head;

    dns_cache_head = entry;
    dns_cache_size++;

    dns_cache_unlock();
}

/**
 * Cleanup expired entries from DNS cache
 */
void dns_cache_cleanup(void) {
    dns_cache_init_mutex();
    dns_cache_lock();

    time_t now = time(NULL);
    dns_cache_entry_t **curr = &dns_cache_head;

    while (*curr) {
        dns_cache_entry_t *entry = *curr;

        if (now >= entry->expires) {
            /* Remove expired entry */
            *curr = entry->next;

            free(entry->hostname);
            addrinfo_deep_free(entry->result);
            free(entry);
            dns_cache_size--;
        } else {
            curr = &entry->next;
        }
    }

    dns_cache_unlock();
}

/**
 * Clear all DNS cache entries (for cleanup)
 */
void dns_cache_clear(void) {
    dns_cache_init_mutex();
    dns_cache_lock();

    while (dns_cache_head) {
        dns_cache_entry_t *next = dns_cache_head->next;

        free(dns_cache_head->hostname);
        addrinfo_deep_free(dns_cache_head->result);
        free(dns_cache_head);

        dns_cache_head = next;
    }

    dns_cache_size = 0;
    dns_cache_unlock();
}

/* ====================================================================
 * TCP CONNECTION WITH HAPPY EYEBALLS (RFC 8305)
 * ==================================================================== */

/* Happy Eyeballs configuration */
#define HAPPY_EYEBALLS_DELAY_MS 250   /* RFC 8305 recommends 250ms */
#define MAX_PARALLEL_CONNECTIONS 2     /* IPv6 + IPv4 */

/**
 * Configure socket with performance options
 */
static void configure_socket_options(int sockfd) {
    /* Enable TCP_NODELAY (disable Nagle's algorithm for lower latency) */
    int nodelay = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));

    /* Enable SO_REUSEADDR for faster socket reuse */
    int reuse = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

    /* Enable SO_KEEPALIVE for connection health monitoring */
    int keepalive = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, (char*)&keepalive, sizeof(keepalive));

    /* Optimize send/receive buffer sizes (64KB each for better throughput) */
    int bufsize = 65536;
    setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (char*)&bufsize, sizeof(bufsize));
    setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (char*)&bufsize, sizeof(bufsize));

#ifdef TCP_QUICKACK
    /* Enable TCP_QUICKACK on Linux for faster ACKs */
    int quickack = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_QUICKACK, (char*)&quickack, sizeof(quickack));
#endif

#ifdef SO_REUSEPORT
    /* Enable SO_REUSEPORT if available (Linux 3.9+, BSD) */
    int reuseport = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, (char*)&reuseport, sizeof(reuseport));
#endif
}

/**
 * Set socket to non-blocking mode
 */
static void set_socket_nonblocking(int sockfd) {
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(sockfd, FIONBIO, &mode);
#else
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
#endif
}

/**
 * Set socket to blocking mode
 */
static void set_socket_blocking(int sockfd) {
#ifdef _WIN32
    u_long mode = 0;
    ioctlsocket(sockfd, FIONBIO, &mode);
#else
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags & ~O_NONBLOCK);
#endif
}

/**
 * Configure final socket options after successful connection
 */
static void configure_connected_socket(int sockfd, uint32_t timeout_ms) {
    /* Set socket to blocking mode for HTTP/1.1 compatibility */
    set_socket_blocking(sockfd);

    /* Set performance options */
    int opt = 1;
#ifdef _WIN32
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char*)&opt, sizeof(opt));
#else
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    /* Enable TCP keep-alive to detect dead connections */
    setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));

    #ifdef TCP_KEEPIDLE
    int keepidle = 60;
    setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
    #endif

    #ifdef TCP_KEEPINTVL
    int keepintvl = 10;
    setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
    #endif

    #ifdef TCP_KEEPCNT
    int keepcnt = 3;
    setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));
    #endif

    #ifdef __APPLE__
    #ifdef TCP_FASTOPEN
    int tfo = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_FASTOPEN, &tfo, sizeof(tfo));
    #endif
    #endif

    #ifdef __linux__
    #ifdef TCP_FASTOPEN_CONNECT
    int tfo = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_FASTOPEN_CONNECT, &tfo, sizeof(tfo));
    #endif
    #endif
#endif

    /* Set receive timeout to prevent indefinite blocking */
#ifdef _WIN32
    DWORD timeout_dw = timeout_ms;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout_dw, sizeof(timeout_dw));
#else
    struct timeval recv_timeout;
    recv_timeout.tv_sec = timeout_ms / 1000;
    recv_timeout.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));
#endif
}

/**
 * Check if socket connection completed (success or failure)
 * Returns: 1 = connected, 0 = still pending, -1 = failed
 */
static int check_socket_connected(int sockfd) {
    int error = 0;
    socklen_t len = sizeof(error);

#ifdef _WIN32
    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, (char*)&error, (int*)&len) != 0) {
        return -1;
    }
#else
    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) != 0) {
        return -1;
    }
#endif

    if (error == 0) {
        return 1;  /* Connected */
    }
    return -1;  /* Failed */
}

/**
 * Start a non-blocking connection attempt
 * Returns: socket fd on success (connection in progress), -1 on immediate failure
 */
static int start_connection_attempt(const struct addrinfo *addr) {
    int sockfd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if (sockfd == -1) {
        return -1;
    }

    configure_socket_options(sockfd);
    set_socket_nonblocking(sockfd);

    int ret = connect(sockfd, addr->ai_addr, addr->ai_addrlen);
    if (ret == 0) {
        /* Connected immediately (rare but possible on localhost) */
        return sockfd;
    }

#ifdef _WIN32
    if (WSAGetLastError() == WSAEWOULDBLOCK) {
        return sockfd;  /* Connection in progress */
    }
#else
    if (errno == EINPROGRESS) {
        return sockfd;  /* Connection in progress */
    }
#endif

    /* Immediate failure */
    close(sockfd);
    return -1;
}

/**
 * Establish a TCP connection using Happy Eyeballs (RFC 8305)
 *
 * Algorithm:
 * 1. Sort addresses: IPv6 first, then IPv4 (interleaved by family)
 * 2. Start first (IPv6) connection immediately
 * 3. After 250ms delay, start IPv4 connection if IPv6 not yet connected
 * 4. Return whichever connection succeeds first
 * 5. Cancel losing connection(s)
 */
int httpmorph_tcp_connect(const char *host, uint16_t port, uint32_t timeout_ms,
                          uint64_t *connect_time_us) {
    struct addrinfo hints, *result;
    int sockfd = -1;
    uint64_t start_time = httpmorph_get_time_us();
    bool need_free_result = false;

    /* Try DNS cache first */
    result = dns_cache_lookup(host, port);
    if (result) {
        need_free_result = true;
    } else {
        /* Cache miss - perform DNS lookup */
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;     /* Allow IPv4 or IPv6 */
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = 0;
        hints.ai_protocol = 0;

        char port_str[6];
        snprintf(port_str, sizeof(port_str), "%u", port);

        int ret = getaddrinfo(host, port_str, &hints, &result);
        if (ret != 0) {
            return -1;
        }

        dns_cache_add(host, port, result);
        need_free_result = false;
    }

    /* Separate addresses by family (RFC 8305: prefer IPv6) */
    struct addrinfo *ipv6_addrs[16];
    struct addrinfo *ipv4_addrs[16];
    int ipv6_count = 0, ipv4_count = 0;

    for (struct addrinfo *rp = result; rp != NULL; rp = rp->ai_next) {
        if (rp->ai_family == AF_INET6 && ipv6_count < 16) {
            ipv6_addrs[ipv6_count++] = rp;
        } else if (rp->ai_family == AF_INET && ipv4_count < 16) {
            ipv4_addrs[ipv4_count++] = rp;
        }
    }

    /* Build interleaved address list (IPv6, IPv4, IPv6, IPv4, ...) per RFC 8305 */
    struct addrinfo *sorted_addrs[32];
    int sorted_count = 0;
    int i6 = 0, i4 = 0;

    while (i6 < ipv6_count || i4 < ipv4_count) {
        if (i6 < ipv6_count) {
            sorted_addrs[sorted_count++] = ipv6_addrs[i6++];
        }
        if (i4 < ipv4_count) {
            sorted_addrs[sorted_count++] = ipv4_addrs[i4++];
        }
    }

    if (sorted_count == 0) {
        /* No addresses found */
        if (need_free_result) {
            addrinfo_deep_free(result);
        } else {
            freeaddrinfo(result);
        }
        return -1;
    }

    /* Happy Eyeballs: race connections with staggered starts */
    int active_sockets[MAX_PARALLEL_CONNECTIONS] = {-1, -1};
    int active_count = 0;
    int next_addr_idx = 0;
    uint64_t next_attempt_time = 0;
    uint64_t deadline = start_time + (uint64_t)timeout_ms * 1000;

    /* Start first connection immediately */
    active_sockets[0] = start_connection_attempt(sorted_addrs[next_addr_idx++]);
    if (active_sockets[0] >= 0) {
        active_count = 1;
        /* Schedule next attempt after 250ms delay */
        next_attempt_time = httpmorph_get_time_us() + HAPPY_EYEBALLS_DELAY_MS * 1000;
    }

    /* Poll until we have a winner or timeout */
    while (active_count > 0) {
        uint64_t now = httpmorph_get_time_us();

        /* Check timeout */
        if (now >= deadline) {
            break;
        }

        /* Start next connection attempt if delay has passed and we have addresses left */
        if (next_addr_idx < sorted_count && active_count < MAX_PARALLEL_CONNECTIONS && now >= next_attempt_time) {
            int new_sock = start_connection_attempt(sorted_addrs[next_addr_idx++]);
            if (new_sock >= 0) {
                active_sockets[active_count++] = new_sock;
                next_attempt_time = now + HAPPY_EYEBALLS_DELAY_MS * 1000;
            }
        }

        /* Build fd_set for select */
        fd_set write_fds;
        FD_ZERO(&write_fds);
        int max_fd = -1;

        for (int i = 0; i < active_count; i++) {
            if (active_sockets[i] >= 0) {
                FD_SET(active_sockets[i], &write_fds);
                if (active_sockets[i] > max_fd) {
                    max_fd = active_sockets[i];
                }
            }
        }

        if (max_fd < 0) {
            break;
        }

        /* Calculate select timeout:
         * - If we have more addresses to try, wait until next_attempt_time
         * - Otherwise wait until deadline
         */
        uint64_t wait_until = deadline;
        if (next_addr_idx < sorted_count && active_count < MAX_PARALLEL_CONNECTIONS) {
            if (next_attempt_time < wait_until) {
                wait_until = next_attempt_time;
            }
        }

        uint64_t wait_us = (wait_until > now) ? (wait_until - now) : 0;
        /* Cap at 50ms for responsiveness */
        if (wait_us > 50000) wait_us = 50000;

        struct timeval tv;
        tv.tv_sec = wait_us / 1000000;
        tv.tv_usec = wait_us % 1000000;

        int sel_ret = select(SELECT_NFDS(max_fd), NULL, &write_fds, NULL, &tv);

        if (sel_ret > 0) {
            /* Check which socket(s) are ready */
            for (int i = 0; i < active_count; i++) {
                if (active_sockets[i] >= 0 && FD_ISSET(active_sockets[i], &write_fds)) {
                    int status = check_socket_connected(active_sockets[i]);
                    if (status == 1) {
                        /* Winner! This socket connected first */
                        sockfd = active_sockets[i];
                        active_sockets[i] = -1;

                        /* Close all other active sockets */
                        for (int j = 0; j < active_count; j++) {
                            if (active_sockets[j] >= 0) {
                                close(active_sockets[j]);
                                active_sockets[j] = -1;
                            }
                        }
                        active_count = 0;
                        break;
                    } else if (status == -1) {
                        /* This socket failed, close it */
                        close(active_sockets[i]);
                        active_sockets[i] = -1;
                    }
                }
            }

            /* Compact the active_sockets array */
            int write_idx = 0;
            for (int i = 0; i < active_count; i++) {
                if (active_sockets[i] >= 0) {
                    active_sockets[write_idx++] = active_sockets[i];
                }
            }
            active_count = write_idx;

            if (sockfd >= 0) {
                break;  /* We have a winner */
            }
        }

        /* If no active connections and we have more addresses, try next */
        if (active_count == 0 && next_addr_idx < sorted_count) {
            int new_sock = start_connection_attempt(sorted_addrs[next_addr_idx++]);
            if (new_sock >= 0) {
                active_sockets[0] = new_sock;
                active_count = 1;
                next_attempt_time = httpmorph_get_time_us() + HAPPY_EYEBALLS_DELAY_MS * 1000;
            }
        }
    }

    /* Cleanup any remaining active sockets */
    for (int i = 0; i < active_count; i++) {
        if (active_sockets[i] >= 0) {
            close(active_sockets[i]);
        }
    }

    /* Free DNS result */
    if (need_free_result) {
        addrinfo_deep_free(result);
    } else {
        freeaddrinfo(result);
    }

    /* Configure winning socket */
    if (sockfd >= 0) {
        configure_connected_socket(sockfd, timeout_ms);
        *connect_time_us = httpmorph_get_time_us() - start_time;
    }

    return sockfd;
}
