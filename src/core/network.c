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
 * Lookup hostname in DNS cache
 * Returns cached addrinfo if found and not expired, NULL otherwise
 */
static struct addrinfo* dns_cache_lookup(const char *hostname, uint16_t port) {
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
static void dns_cache_add(const char *hostname, uint16_t port,
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

    entry->hostname = strdup(hostname);
    entry->port = port;
    entry->result = addrinfo_deep_copy(result);
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
 * TCP CONNECTION
 * ==================================================================== */

/**
 * Establish a TCP connection to a host
 */
int httpmorph_tcp_connect(const char *host, uint16_t port, uint32_t timeout_ms,
                          uint64_t *connect_time_us) {
    struct addrinfo hints, *result, *rp;
    int sockfd = -1;
    uint64_t start_time = httpmorph_get_time_us();
    bool need_free_result = false;

    /* Try DNS cache first */
    result = dns_cache_lookup(host, port);
    if (result) {
        need_free_result = true;  /* We own this copy */
    } else {
        /* Cache miss - perform DNS lookup */
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;     /* Allow IPv4 or IPv6 */
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = 0;
        hints.ai_protocol = 0;

        /* Convert port to string */
        char port_str[6];
        snprintf(port_str, sizeof(port_str), "%u", port);

        /* Resolve hostname */
        int ret = getaddrinfo(host, port_str, &hints, &result);
        if (ret != 0) {
            return -1;
        }

        /* Add to cache for future use */
        dns_cache_add(host, port, result);
        need_free_result = false;  /* Will use freeaddrinfo() */
    }

    /* Try each address until we succeed */
    int ret;
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1) {
            continue;
        }

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

        /* Set socket to non-blocking for timeout support */
#ifdef _WIN32
        u_long mode = 1;
        ioctlsocket(sockfd, FIONBIO, &mode);
#else
        int flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
#endif

        /* Attempt connection */
        ret = connect(sockfd, rp->ai_addr, rp->ai_addrlen);
        if (ret == 0) {
            /* Connected immediately */
            break;
        }

#ifndef _WIN32
        /* On Unix, check for immediate connection failure */
        if (errno == ECONNREFUSED || errno == ENETUNREACH || errno == EHOSTUNREACH ||
            errno == ETIMEDOUT || errno == ECONNRESET) {
            /* Connection failed immediately - don't wait, try next address */
            if (sockfd > 2) close(sockfd);
            sockfd = -1;
            continue;
        }
#endif

#ifdef _WIN32
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
#else
        if (errno == EINPROGRESS) {
#endif
            /* Connection in progress - wait with select using polling approach */
            uint64_t poll_start = httpmorph_get_time_us();
            uint64_t poll_timeout_us = (uint64_t)timeout_ms * 1000;
            int connected = 0;

            while (httpmorph_get_time_us() - poll_start < poll_timeout_us) {
                fd_set write_fds, except_fds;
                struct timeval tv;

                FD_ZERO(&write_fds);
                FD_ZERO(&except_fds);
                FD_SET(sockfd, &write_fds);
                FD_SET(sockfd, &except_fds);

                /* Poll every 100ms to detect errors quickly */
                tv.tv_sec = 0;
                tv.tv_usec = 100000;  /* 100ms */

                ret = select(SELECT_NFDS(sockfd), NULL, &write_fds, &except_fds, &tv);

                /* Check socket error after each poll */
                int error = 0;
                socklen_t len = sizeof(error);
#ifdef _WIN32
                if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, (char*)&error, (int*)&len) == 0) {
#else
                if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) == 0) {
#endif
                    if (error == 0 && ret > 0 && (FD_ISSET(sockfd, &write_fds) || FD_ISSET(sockfd, &except_fds))) {
                        /* Connection succeeded */
                        connected = 1;
                        break;
                    } else if (error != 0) {
                        /* Connection failed - don't wait, try next address */
                        break;
                    }
                }
            }

            if (connected) {
                break;  /* Successfully connected */
            }
            /* Connection failed or timed out - try next address */
        }

        /* Connection failed, try next address */
        if (sockfd > 2) close(sockfd);
        sockfd = -1;
    }

    /* Free result using appropriate method */
    if (need_free_result) {
        addrinfo_deep_free(result);
    } else {
        freeaddrinfo(result);
    }

    if (sockfd != -1) {
        /* Set socket to blocking mode for HTTP/1.1 compatibility
         * (HTTP/2 will set it back to non-blocking later if negotiated) */
#ifdef _WIN32
        u_long mode = 0;
        ioctlsocket(sockfd, FIONBIO, &mode);
#else
        int flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags & ~O_NONBLOCK);
#endif

        /* Set performance options */
        int opt = 1;
#ifdef _WIN32
        setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char*)&opt, sizeof(opt));
#else
        setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

        /* Enable TCP keep-alive to detect dead connections */
        setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));

        #ifdef TCP_KEEPIDLE
        /* Start probing after 60 seconds of idle time */
        int keepidle = 60;
        setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
        #endif

        #ifdef TCP_KEEPINTVL
        /* Send probes every 10 seconds */
        int keepintvl = 10;
        setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
        #endif

        #ifdef TCP_KEEPCNT
        /* Drop connection after 3 failed probes */
        int keepcnt = 3;
        setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));
        #endif

        #ifdef __APPLE__
        /* Enable TCP Fast Open on macOS for reduced latency */
        #ifdef TCP_FASTOPEN
        int tfo = 1;
        setsockopt(sockfd, IPPROTO_TCP, TCP_FASTOPEN, &tfo, sizeof(tfo));
        #endif
        #endif

        #ifdef __linux__
        /* Enable TCP Fast Open on Linux */
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

        *connect_time_us = httpmorph_get_time_us() - start_time;
    }

    return sockfd;
}
