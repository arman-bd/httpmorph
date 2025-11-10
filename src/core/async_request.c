/**
 * async_request.c - Async request state machine implementation
 */

/* Define feature test macros before any includes */
#ifndef _WIN32
    #define _POSIX_C_SOURCE 200809L
    #define _DEFAULT_SOURCE
#endif

#include "async_request.h"
#include "io_engine.h"
#include "internal/proxy.h"
#include "internal/util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* SSL/TLS support */
#include <openssl/ssl.h>
#include <openssl/err.h>

/* Debug output control */
#ifdef HTTPMORPH_DEBUG
    #define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
    #define DEBUG_PRINT(...) ((void)0)
#endif

/* Platform-specific headers */
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <mswsock.h>   /* For ConnectEx, AcceptEx, etc. */

    /* Extension function pointers (loaded dynamically) */
    static LPFN_CONNECTEX pfnConnectEx = NULL;
    static bool wsa_extensions_loaded = false;
#else
    #include <unistd.h>
    #include <errno.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netdb.h>
    #include <sys/time.h>
#endif

/* Default buffer sizes */
#define SEND_BUFFER_SIZE (64 * 1024)      /* 64KB */
#define RECV_BUFFER_SIZE (256 * 1024)     /* 256KB */

/* ID generation */
static uint64_t next_request_id = 1;

/**
 * Get current time in microseconds
 */
static uint64_t get_time_us(void) {
#ifdef _WIN32
    FILETIME ft;
    ULARGE_INTEGER uli;

    /* Get current time as FILETIME (100-nanosecond intervals since 1601-01-01) */
    GetSystemTimeAsFileTime(&ft);

    /* Convert to 64-bit integer */
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;

    /* Convert to microseconds since Unix epoch (1970-01-01) */
    /* FILETIME epoch is 1601-01-01, Unix epoch is 1970-01-01 */
    /* Difference is 11644473600 seconds */
    uint64_t microseconds = uli.QuadPart / 10; /* Convert 100-ns to microseconds */
    microseconds -= 11644473600ULL * 1000000ULL; /* Adjust to Unix epoch */

    return microseconds;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + (uint64_t)tv.tv_usec;
#endif
}

/**
 * Initialize Windows Socket Extensions (ConnectEx, etc.)
 */
#ifdef _WIN32
static int init_wsa_extensions(int sockfd) {
    if (wsa_extensions_loaded) {
        return 0;  /* Already loaded */
    }

    GUID guid_connectex = WSAID_CONNECTEX;
    DWORD bytes = 0;

    /* Load ConnectEx function pointer */
    if (WSAIoctl(sockfd, SIO_GET_EXTENSION_FUNCTION_POINTER,
                 &guid_connectex, sizeof(guid_connectex),
                 &pfnConnectEx, sizeof(pfnConnectEx),
                 &bytes, NULL, NULL) == SOCKET_ERROR) {
        DEBUG_PRINT("[async_request] Failed to load ConnectEx: %d\n", WSAGetLastError());
        return -1;
    }

    wsa_extensions_loaded = true;
    DEBUG_PRINT("[async_request] WSA extensions loaded successfully\n");
    return 0;
}

/**
 * Allocate and initialize OVERLAPPED structure for IOCP
 * Note: For IOCP, hEvent should be NULL as IOCP uses completion ports
 */
static void* alloc_overlapped(void) {
    OVERLAPPED *ov = (OVERLAPPED*)calloc(1, sizeof(OVERLAPPED));
    /* For IOCP, don't create an event - IOCP uses the completion port */
    /* hEvent is already NULL from calloc */
    return ov;
}

/**
 * Free OVERLAPPED structure
 */
static void free_overlapped(void *overlapped) {
    if (overlapped) {
        free(overlapped);
    }
}
#endif

/**
 * Get state name for debugging
 */
const char* async_request_state_name(async_request_state_t state) {
    switch (state) {
        case ASYNC_STATE_INIT:              return "INIT";
        case ASYNC_STATE_DNS_LOOKUP:        return "DNS_LOOKUP";
        case ASYNC_STATE_CONNECTING:        return "CONNECTING";
        case ASYNC_STATE_PROXY_CONNECT:     return "PROXY_CONNECT";
        case ASYNC_STATE_TLS_HANDSHAKE:     return "TLS_HANDSHAKE";
        case ASYNC_STATE_SENDING_REQUEST:   return "SENDING_REQUEST";
        case ASYNC_STATE_RECEIVING_HEADERS: return "RECEIVING_HEADERS";
        case ASYNC_STATE_RECEIVING_BODY:    return "RECEIVING_BODY";
        case ASYNC_STATE_COMPLETE:          return "COMPLETE";
        case ASYNC_STATE_ERROR:             return "ERROR";
        default:                            return "UNKNOWN";
    }
}

/**
 * Create a new async request
 */
async_request_t* async_request_create(
    const httpmorph_request_t *request,
    io_engine_t *io_engine,
    SSL_CTX *ssl_ctx,
    uint32_t timeout_ms,
    async_request_callback_t callback,
    void *user_data)
{
    if (!request || !io_engine) {
        return NULL;
    }

    async_request_t *req = calloc(1, sizeof(async_request_t));
    if (!req) {
        return NULL;
    }

    /* Initialize basic fields */
    req->id = next_request_id++;
    req->state = ASYNC_STATE_INIT;
    req->request = (httpmorph_request_t*)request;  /* Store pointer */
    req->io_engine = io_engine;
    req->sockfd = -1;
    req->ssl = NULL;
    req->refcount = 1;
    req->dns_resolved = false;

    /* Determine if HTTPS first (needed before creating SSL) */
    req->is_https = request->use_tls;

    /* Timing */
    req->start_time_us = get_time_us();
    req->timeout_ms = timeout_ms;
    if (timeout_ms > 0) {
        req->deadline_us = req->start_time_us + (uint64_t)timeout_ms * 1000;
    } else {
        req->deadline_us = 0;  /* No timeout */
    }

    /* Allocate send buffer */
    req->send_buf = malloc(SEND_BUFFER_SIZE);
    if (!req->send_buf) {
        free(req);
        return NULL;
    }

    /* Allocate receive buffer */
    req->recv_buf = malloc(RECV_BUFFER_SIZE);
    if (!req->recv_buf) {
        free(req->send_buf);
        free(req);
        return NULL;
    }
    req->recv_capacity = RECV_BUFFER_SIZE;

    /* Set callback */
    req->on_complete = callback;
    req->user_data = user_data;

    /* Initialize Windows-specific fields */
#ifdef _WIN32
    req->overlapped_connect = NULL;
    req->overlapped_send = NULL;
    req->overlapped_recv = NULL;
    req->iocp_operation_pending = false;
    req->iocp_last_error = 0;
    req->iocp_bytes_transferred = 0;
    req->iocp_completion_callback = NULL;

    /* Create completion event (manual-reset, initially non-signaled) */
    req->iocp_completion_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!req->iocp_completion_event) {
        DEBUG_PRINT("[async_request] Failed to create completion event\n");
        free(req->recv_buf);
        free(req);
        return NULL;
    }
#endif

    /* Parse URL to extract hostname and port if not already set */
    if (!request->host && request->url) {
        char *scheme = NULL, *host = NULL, *path = NULL;
        uint16_t port = 0;

        extern int httpmorph_parse_url(const char *url, char **scheme, char **host, uint16_t *port, char **path);

        if (httpmorph_parse_url(request->url, &scheme, &host, &port, &path) == 0) {
            /* Store parsed values in request structure */
            ((httpmorph_request_t*)request)->host = host;  /* Transfer ownership */
            ((httpmorph_request_t*)request)->port = port;
            ((httpmorph_request_t*)request)->use_tls = (scheme && strcmp(scheme, "https") == 0);
            req->is_https = ((httpmorph_request_t*)request)->use_tls;

            /* Free scheme and path as we don't need them */
            free(scheme);
            free(path);
        }
    }

    /* Initialize proxy fields */
    req->using_proxy = false;
    req->proxy_host = NULL;
    req->proxy_port = 0;
    req->proxy_username = NULL;
    req->proxy_password = NULL;
    req->proxy_use_tls = false;
    req->target_host = NULL;
    req->target_port = 0;
    req->proxy_connect_sent = false;
    req->proxy_recv_buf = NULL;
    req->proxy_recv_len = 0;

    /* Parse and configure proxy if provided */
    if (request->proxy_url && request->proxy_url[0] != '\0') {
        /* Parse proxy URL */
        if (httpmorph_parse_proxy_url(
                request->proxy_url,
                &req->proxy_host,
                &req->proxy_port,
                &req->proxy_username,
                &req->proxy_password,
                &req->proxy_use_tls) == 0) {

            req->using_proxy = true;

            /* Override with explicit proxy credentials if provided */
            if (request->proxy_username) {
                free(req->proxy_username);
                req->proxy_username = strdup(request->proxy_username);
            }
            if (request->proxy_password) {
                free(req->proxy_password);
                req->proxy_password = strdup(request->proxy_password);
            }

            /* Store original target host/port */
            if (request->host) {
                req->target_host = strdup(request->host);
                req->target_port = request->port;
            }

            /* Allocate proxy receive buffer for CONNECT response */
            req->proxy_recv_buf = malloc(4096);
            if (!req->proxy_recv_buf) {
                /* Cleanup on failure */
                free(req->proxy_host);
                free(req->proxy_username);
                free(req->proxy_password);
                free(req->target_host);
                free(req->send_buf);
                free(req->recv_buf);
                free(req);
                return NULL;
            }

            DEBUG_PRINT("[async_request] Using proxy %s:%u for target %s:%u (id=%lu)\n",
                   req->proxy_host, req->proxy_port,
                   req->target_host, req->target_port,
                   (unsigned long)req->id);
        }
    }

    /* Create SSL object for HTTPS requests */
    if (req->is_https && ssl_ctx) {
        req->ssl = SSL_new(ssl_ctx);
        if (!req->ssl) {
            /* Cleanup and return NULL */
            free(req->send_buf);
            free(req->recv_buf);
            free(req);
            return NULL;
        }

        /* Set SSL verification mode based on request setting */
        if (request->verify_ssl) {
            SSL_set_verify(req->ssl, SSL_VERIFY_PEER, NULL);
        } else {
            SSL_set_verify(req->ssl, SSL_VERIFY_NONE, NULL);
        }

        /* Set SNI hostname if available */
        if (request->host) {
            SSL_set_tlsext_host_name(req->ssl, request->host);
        }

        /* Set SSL to non-blocking mode (will be done when socket is created) */
        DEBUG_PRINT("[async_request] Created SSL object for HTTPS (id=%lu)\n",
               (unsigned long)req->id);
    }

    return req;
}

/**
 * Destroy an async request
 */
void async_request_destroy(async_request_t *req) {
    if (!req) {
        return;
    }

    /* Clean up I/O operation */
    if (req->current_op) {
        io_op_destroy(req->current_op);
        req->current_op = NULL;
    }

    /* Clean up Windows OVERLAPPED structures and event */
#ifdef _WIN32
    free_overlapped(req->overlapped_connect);
    free_overlapped(req->overlapped_send);
    free_overlapped(req->overlapped_recv);
    req->overlapped_connect = NULL;
    req->overlapped_send = NULL;
    req->overlapped_recv = NULL;

    /* Close completion event */
    if (req->iocp_completion_event) {
        CloseHandle((HANDLE)req->iocp_completion_event);
        req->iocp_completion_event = NULL;
    }
#endif

    /* Close socket (but never close stdin/stdout/stderr) */
    if (req->sockfd > 2) {
#ifdef _WIN32
        closesocket(req->sockfd);
#else
        close(req->sockfd);
#endif
        req->sockfd = -1;
    }

    /* Clean up SSL */
    if (req->ssl) {
        SSL_free(req->ssl);
        req->ssl = NULL;
    }

    /* Free buffers */
    free(req->send_buf);
    free(req->recv_buf);

    /* Free proxy buffers and strings */
    free(req->proxy_host);
    free(req->proxy_username);
    free(req->proxy_password);
    free(req->target_host);
    free(req->proxy_recv_buf);

    /* Free response if allocated */
    if (req->response) {
        httpmorph_response_destroy(req->response);
        req->response = NULL;
    }

    free(req);
}

/**
 * Reference counting
 */
void async_request_ref(async_request_t *req) {
    if (req) {
        req->refcount++;
    }
}

void async_request_unref(async_request_t *req) {
    if (req) {
        req->refcount--;
        if (req->refcount <= 0) {
            async_request_destroy(req);
        }
    }
}

/**
 * Get current state
 */
async_request_state_t async_request_get_state(const async_request_t *req) {
    return req ? req->state : ASYNC_STATE_ERROR;
}

/**
 * Get file descriptor
 */
int async_request_get_fd(const async_request_t *req) {
    return req ? req->sockfd : -1;
}

/**
 * Check timeout
 */
bool async_request_is_timeout(const async_request_t *req) {
    if (!req || req->deadline_us == 0) {
        return false;
    }
    return get_time_us() >= req->deadline_us;
}

/**
 * Set error state
 */
void async_request_set_error(async_request_t *req, int error_code, const char *error_msg) {
    if (!req) {
        return;
    }

    req->state = ASYNC_STATE_ERROR;
    req->error_code = error_code;
    if (error_msg) {
        snprintf(req->error_msg, sizeof(req->error_msg), "%s", error_msg);
    }
}

/**
 * Get response
 */
httpmorph_response_t* async_request_get_response(async_request_t *req) {
    if (!req || req->state != ASYNC_STATE_COMPLETE) {
        return NULL;
    }
    return req->response;
}

/**
 * Get error message
 */
const char* async_request_get_error_message(const async_request_t *req) {
    if (!req || req->error_msg[0] == '\0') {
        return "No error message";
    }
    return req->error_msg;
}

/**
 * State: DNS lookup
 */
static int step_dns_lookup(async_request_t *req) {
    /* Check if already resolved */
    if (req->dns_resolved) {
        req->state = ASYNC_STATE_CONNECTING;
        return ASYNC_STATUS_IN_PROGRESS;
    }

    /* Perform blocking DNS lookup (for now) */
    /* Note: In production, this should use async DNS (getaddrinfo_a or thread pool) */

    /* If using proxy, resolve proxy hostname instead of target hostname */
    const char *hostname;
    uint16_t port;

    if (req->using_proxy) {
        hostname = req->proxy_host;
        port = req->proxy_port;
        DEBUG_PRINT("[async_request] Resolving proxy %s:%u (target: %s:%u) (id=%lu)\n",
               hostname, port, req->target_host, req->target_port, (unsigned long)req->id);
    } else {
        hostname = req->request->host;
        port = req->request->port;
        DEBUG_PRINT("[async_request] Resolving %s:%u (id=%lu)\n",
               hostname, port, (unsigned long)req->id);
    }

    if (!hostname) {
        async_request_set_error(req, -1, "No hostname specified");
        return ASYNC_STATUS_ERROR;
    }

    /* Setup hints for getaddrinfo */
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM; /* TCP socket */
    hints.ai_flags = AI_ADDRCONFIG;  /* Only return addresses we can use */

    /* Convert port to string */
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%u", port);

    /* Perform DNS lookup */
    struct addrinfo *result = NULL;
    int ret = getaddrinfo(hostname, port_str, &hints, &result);

    if (ret != 0) {
        char error_buf[256];
        snprintf(error_buf, sizeof(error_buf), "DNS lookup failed: %s", gai_strerror(ret));
        async_request_set_error(req, ret, error_buf);
        return ASYNC_STATUS_ERROR;
    }

    if (!result) {
        async_request_set_error(req, -1, "DNS lookup returned no results");
        return ASYNC_STATUS_ERROR;
    }

    /* Store the first result */
    memcpy(&req->addr, result->ai_addr, result->ai_addrlen);
    req->addr_len = result->ai_addrlen;
    req->dns_resolved = true;

    DEBUG_PRINT("[async_request] DNS resolved for %s:%u (id=%lu)\n",
           hostname, port, (unsigned long)req->id);

    /* Free the result */
    freeaddrinfo(result);

    /* Move to connecting state */
    req->state = ASYNC_STATE_CONNECTING;
    return ASYNC_STATUS_IN_PROGRESS;
}

/**
 * State: Connecting
 */
static int step_connecting(async_request_t *req) {
    /* Verify DNS was resolved */
    if (!req->dns_resolved) {
        async_request_set_error(req, -1, "DNS not resolved before connect");
        return ASYNC_STATUS_ERROR;
    }

    /* If socket not created yet, create it and initiate connection */
    if (req->sockfd < 0) {
        /* Get address family from resolved address */
        int af = ((struct sockaddr*)&req->addr)->sa_family;

        /* Create non-blocking socket */
        req->sockfd = io_socket_create_nonblocking(af, SOCK_STREAM, 0);
        if (req->sockfd < 0) {
            async_request_set_error(req, errno, "Failed to create socket");
            return ASYNC_STATUS_ERROR;
        }

        /* Set performance options */
        io_socket_set_performance_opts(req->sockfd);

        DEBUG_PRINT("[async_request] Connecting to %s:%u on fd=%d (id=%lu)\n",
               req->request->host, req->request->port, req->sockfd, (unsigned long)req->id);

#ifdef _WIN32
        /* Windows IOCP path with ConnectEx - skip for SSL sockets */
        if (req->io_engine && req->io_engine->type == IO_ENGINE_IOCP && !req->is_https) {
            /* Initialize WSA extensions if needed */
            if (init_wsa_extensions(req->sockfd) < 0) {
                async_request_set_error(req, -1, "Failed to load WSA extensions");
                return ASYNC_STATUS_ERROR;
            }

            /* ConnectEx requires socket to be bound first */
            struct sockaddr_storage local_addr;
            memset(&local_addr, 0, sizeof(local_addr));
            local_addr.ss_family = af;

            if (bind(req->sockfd, (struct sockaddr*)&local_addr,
                    af == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6)) == SOCKET_ERROR) {
                req->iocp_last_error = WSAGetLastError();
                char error_buf[256];
                snprintf(error_buf, sizeof(error_buf), "Bind failed: %d", req->iocp_last_error);
                async_request_set_error(req, req->iocp_last_error, error_buf);
                return ASYNC_STATUS_ERROR;
            }

            /* Associate socket with IOCP ONLY if NOT using SSL */
            /* SSL sockets CANNOT use IOCP because SSL_write/SSL_read use regular I/O */
            if (!req->is_https) {
                if (CreateIoCompletionPort((HANDLE)req->sockfd, (HANDLE)req->io_engine->iocp_handle,
                                          (ULONG_PTR)req, 0) == NULL) {
                    req->iocp_last_error = GetLastError();
                    char error_buf[256];
                    snprintf(error_buf, sizeof(error_buf), "Failed to associate socket with IOCP: %d",
                            req->iocp_last_error);
                    async_request_set_error(req, req->iocp_last_error, error_buf);
                    return ASYNC_STATUS_ERROR;
                }
                DEBUG_PRINT("[async_request] Socket fd=%d associated with IOCP, completion_key=%p (id=%lu)\n",
                       req->sockfd, (void*)req, (unsigned long)req->id);
            } else {
                DEBUG_PRINT("[async_request] Socket fd=%d NOT associated with IOCP (SSL socket) (id=%lu)\n",
                       req->sockfd, (unsigned long)req->id);
            }

            /* Allocate OVERLAPPED structure */
            if (!req->overlapped_connect) {
                req->overlapped_connect = alloc_overlapped();
                if (!req->overlapped_connect) {
                    async_request_set_error(req, -1, "Failed to allocate OVERLAPPED");
                    return ASYNC_STATUS_ERROR;
                }
            }

            /* Reset completion event before starting new operation */
            ResetEvent((HANDLE)req->iocp_completion_event);

            /* Start async connect */
            BOOL result = pfnConnectEx(req->sockfd, (struct sockaddr*)&req->addr, req->addr_len,
                                       NULL, 0, NULL, (OVERLAPPED*)req->overlapped_connect);

            if (!result) {
                req->iocp_last_error = WSAGetLastError();
                if (req->iocp_last_error == ERROR_IO_PENDING) {
                    /* Async operation started successfully */
                    req->iocp_operation_pending = true;
                    DEBUG_PRINT("[async_request] ConnectEx started (id=%lu)\n", (unsigned long)req->id);
                    return ASYNC_STATUS_NEED_WRITE;
                } else {
                    /* Connect failed immediately */
                    char error_buf[256];
                    snprintf(error_buf, sizeof(error_buf), "ConnectEx failed: %d", req->iocp_last_error);
                    async_request_set_error(req, req->iocp_last_error, error_buf);
                    return ASYNC_STATUS_ERROR;
                }
            }

            /* Connected immediately (rare) */
            req->iocp_operation_pending = false;
            DEBUG_PRINT("[async_request] ConnectEx completed immediately (id=%lu)\n", (unsigned long)req->id);

            /* Update socket context (required after ConnectEx) */
            setsockopt(req->sockfd, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);

            /* Move to next state */
            if (req->using_proxy && req->is_https) {
                /* HTTPS via proxy: establish tunnel with CONNECT */
                req->state = ASYNC_STATE_PROXY_CONNECT;
            } else if (req->using_proxy && !req->is_https) {
                /* HTTP via proxy: send request directly to proxy (no CONNECT needed) */
                req->state = ASYNC_STATE_SENDING_REQUEST;
            } else if (req->is_https) {
                /* Direct HTTPS connection, do TLS handshake */
                req->state = ASYNC_STATE_TLS_HANDSHAKE;
            } else {
                /* Direct HTTP connection, send request */
                req->state = ASYNC_STATE_SENDING_REQUEST;
            }
            return ASYNC_STATUS_IN_PROGRESS;
        }
#endif

        /* Non-Windows or non-IOCP path: standard non-blocking connect */
        int ret = connect(req->sockfd, (struct sockaddr*)&req->addr, req->addr_len);

        if (ret == 0) {
            /* Connected immediately (rare for non-blocking) */
            DEBUG_PRINT("[async_request] Connected immediately (id=%lu)\n",
                   (unsigned long)req->id);

            /* Move to next state */
            if (req->using_proxy && req->is_https) {
                /* HTTPS via proxy: establish tunnel with CONNECT */
                req->state = ASYNC_STATE_PROXY_CONNECT;
            } else if (req->using_proxy && !req->is_https) {
                /* HTTP via proxy: send request directly to proxy (no CONNECT needed) */
                req->state = ASYNC_STATE_SENDING_REQUEST;
            } else if (req->is_https) {
                /* Direct HTTPS connection, do TLS handshake */
                req->state = ASYNC_STATE_TLS_HANDSHAKE;
            } else {
                /* Direct HTTP connection, send request */
                req->state = ASYNC_STATE_SENDING_REQUEST;
            }
            return ASYNC_STATUS_IN_PROGRESS;
        }

#ifndef _WIN32
        if (errno == EINPROGRESS) {
            /* Connection in progress, wait for writable */
            return ASYNC_STATUS_NEED_WRITE;
        }
#else
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
            /* Connection in progress, wait for writable */
            return ASYNC_STATUS_NEED_WRITE;
        }
#endif

        /* Connect failed immediately */
        async_request_set_error(req, errno, "Connection failed");
        return ASYNC_STATUS_ERROR;
    }

    /* Socket already exists, check if connect completed */
#ifdef _WIN32
    /* For HTTPS (SSL) sockets, skip IOCP and use regular getsockopt check below */
    if (req->io_engine && req->io_engine->type == IO_ENGINE_IOCP && !req->is_https &&
        req->overlapped_connect != NULL) {
        /* IOCP path: check the completion event (non-blocking) */
        DWORD wait_result = WaitForSingleObject((HANDLE)req->iocp_completion_event, 0);

        if (wait_result == WAIT_OBJECT_0) {
            /* Operation completed - process result */
            DEBUG_PRINT("[async_request] ConnectEx completed via dispatcher (error=%d, bytes=%lu, id=%lu)\n",
                   req->iocp_last_error, (unsigned long)req->iocp_bytes_transferred, (unsigned long)req->id);

            /* Reset event for next operation */
            ResetEvent((HANDLE)req->iocp_completion_event);

            /* Check socket error to get actual connection result */
            int sock_error = 0;
            socklen_t len = sizeof(sock_error);
            if (getsockopt(req->sockfd, SOL_SOCKET, SO_ERROR, (char*)&sock_error, &len) == 0) {
                if (sock_error == 0 || sock_error == ERROR_IO_PENDING) {
                    /* Connection successful (ERROR_IO_PENDING is ok, means it's now connected) */
                    /* Update socket context (required after ConnectEx) */
                    setsockopt(req->sockfd, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);

                    /* Move to next state */
                    if (req->using_proxy && req->is_https) {
                        /* HTTPS via proxy: establish tunnel with CONNECT */
                        req->state = ASYNC_STATE_PROXY_CONNECT;
                        /* Wait one more cycle to ensure socket is fully ready */
                        return ASYNC_STATUS_NEED_WRITE;
                    } else if (req->using_proxy && !req->is_https) {
                        /* HTTP via proxy: send request directly to proxy (no CONNECT needed) */
                        req->state = ASYNC_STATE_SENDING_REQUEST;
                    } else if (req->is_https) {
                        /* Direct HTTPS connection, do TLS handshake */
                        req->state = ASYNC_STATE_TLS_HANDSHAKE;
                    } else {
                        /* Direct HTTP connection, send request */
                        req->state = ASYNC_STATE_SENDING_REQUEST;
                    }
                    return ASYNC_STATUS_IN_PROGRESS;
                } else {
                    /* Connection failed */
                    char error_buf[256];
                    snprintf(error_buf, sizeof(error_buf), "ConnectEx failed: %d", sock_error);
                    async_request_set_error(req, sock_error, error_buf);
                    return ASYNC_STATUS_ERROR;
                }
            } else {
                /* Failed to get socket error */
                char error_buf[256];
                snprintf(error_buf, sizeof(error_buf), "Failed to get socket error after ConnectEx");
                async_request_set_error(req, -1, error_buf);
                return ASYNC_STATUS_ERROR;
            }
        } else if (wait_result == WAIT_TIMEOUT) {
            /* Still pending */
            return ASYNC_STATUS_NEED_WRITE;
        } else {
            /* Wait failed */
            req->iocp_last_error = GetLastError();
            char error_buf[256];
            snprintf(error_buf, sizeof(error_buf), "WaitForSingleObject failed: %d", req->iocp_last_error);
            async_request_set_error(req, req->iocp_last_error, error_buf);
            return ASYNC_STATUS_ERROR;
        }
    }

    /* For Windows SSL sockets (not using IOCP), check connect completion */
    if (req->is_https) {
        /* Use WSAPoll to check if socket is writable (connected) */
        WSAPOLLFD poll_fd;
        poll_fd.fd = req->sockfd;
        poll_fd.events = POLLOUT;  /* Check if writable */
        poll_fd.revents = 0;

        int poll_ret = WSAPoll(&poll_fd, 1, 0);  /* 0 timeout = non-blocking */

        if (poll_ret > 0) {
            /* Socket has activity */
            if (poll_fd.revents & POLLERR) {
                /* Socket has an error */
                int error = 0;
                socklen_t len = sizeof(error);
                getsockopt(req->sockfd, SOL_SOCKET, SO_ERROR, (char*)&error, &len);
                char error_buf[256];
                snprintf(error_buf, sizeof(error_buf), "Connection failed: %d", error);
                async_request_set_error(req, error, error_buf);
                return ASYNC_STATUS_ERROR;
            }

            if (poll_fd.revents & POLLOUT) {
                /* Socket is writable - verify connection succeeded */
                int error = 0;
                socklen_t len = sizeof(error);
                if (getsockopt(req->sockfd, SOL_SOCKET, SO_ERROR, (char*)&error, &len) == 0 && error == 0) {
                    /* Connection successful */
                    DEBUG_PRINT("[async_request] Connected successfully (SSL socket) (id=%lu)\n",
                           (unsigned long)req->id);

                    /* Move to next state */
                    if (req->using_proxy && req->is_https) {
                        /* HTTPS via proxy: establish tunnel with CONNECT */
                        req->state = ASYNC_STATE_PROXY_CONNECT;
                        /* Wait one more cycle to ensure socket is fully ready */
                        return ASYNC_STATUS_NEED_WRITE;
                    } else if (req->using_proxy && !req->is_https) {
                        /* HTTP via proxy: send request directly (no CONNECT needed) */
                        req->state = ASYNC_STATE_SENDING_REQUEST;
                    } else {
                        /* Direct HTTPS connection, do TLS handshake */
                        req->state = ASYNC_STATE_TLS_HANDSHAKE;
                    }
                    return ASYNC_STATUS_IN_PROGRESS;
                } else if (error != 0) {
                    /* Connection failed */
                    char error_buf[256];
                    snprintf(error_buf, sizeof(error_buf), "Connection failed: %d", error);
                    async_request_set_error(req, error, error_buf);
                    return ASYNC_STATUS_ERROR;
                }
            }
        } else if (poll_ret < 0) {
            /* Poll error */
            int last_error = WSAGetLastError();
            char error_buf[256];
            snprintf(error_buf, sizeof(error_buf), "WSAPoll failed: %d", last_error);
            async_request_set_error(req, last_error, error_buf);
            return ASYNC_STATUS_ERROR;
        }

        /* Still connecting */
        return ASYNC_STATUS_NEED_WRITE;
    }
#endif

#ifndef _WIN32
    int error = 0;
    socklen_t len = sizeof(error);
    if (getsockopt(req->sockfd, SOL_SOCKET, SO_ERROR, &error, &len) == 0) {
        if (error == 0) {
            /* Connection successful */
            DEBUG_PRINT("[async_request] Connected successfully (id=%lu)\n",
                   (unsigned long)req->id);

            /* Move to next state */
            if (req->using_proxy && req->is_https) {
                /* HTTPS via proxy: establish tunnel with CONNECT */
                req->state = ASYNC_STATE_PROXY_CONNECT;
            } else if (req->using_proxy && !req->is_https) {
                /* HTTP via proxy: send request directly to proxy (no CONNECT needed) */
                req->state = ASYNC_STATE_SENDING_REQUEST;
                /* For HTTP proxy, wait for socket to be writable before sending */
                return ASYNC_STATUS_NEED_WRITE;
            } else if (req->is_https) {
                /* Direct HTTPS connection, do TLS handshake */
                req->state = ASYNC_STATE_TLS_HANDSHAKE;
            } else {
                /* Direct HTTP connection, send request */
                req->state = ASYNC_STATE_SENDING_REQUEST;
                /* For plain HTTP, wait for socket to be writable before sending */
                return ASYNC_STATUS_NEED_WRITE;
            }
            return ASYNC_STATUS_IN_PROGRESS;
        } else if (error == EINPROGRESS || error == EALREADY) {
            /* Still connecting, need to wait */
            return ASYNC_STATUS_NEED_WRITE;
        } else {
            /* Connection failed */
            char error_buf[256];
            snprintf(error_buf, sizeof(error_buf), "Connection failed: %s", strerror(error));
            async_request_set_error(req, error, error_buf);
            return ASYNC_STATUS_ERROR;
        }
    }
#endif

    /* Waiting for connection to complete */
    return ASYNC_STATUS_NEED_WRITE;
}

/**
 * State: TLS handshake
 */
static int step_tls_handshake(async_request_t *req) {
    /* SSL object should exist for HTTPS */
    if (!req->ssl) {
        async_request_set_error(req, -1, "No SSL object for HTTPS");
        return ASYNC_STATUS_ERROR;
    }

    /* Bind SSL to socket if not already done */
    if (SSL_get_fd(req->ssl) != req->sockfd) {
        if (SSL_set_fd(req->ssl, req->sockfd) != 1) {
            async_request_set_error(req, -1, "Failed to bind SSL to socket");
            return ASYNC_STATUS_ERROR;
        }
        /* Set connect state (client mode) */
        SSL_set_connect_state(req->ssl);
        DEBUG_PRINT("[async_request] SSL bound to socket fd=%d (id=%lu)\n",
               req->sockfd, (unsigned long)req->id);
    }

    /* Perform non-blocking handshake */
    int ret = SSL_do_handshake(req->ssl);

    if (ret == 1) {
        /* Handshake complete */
        DEBUG_PRINT("[async_request] TLS handshake complete (id=%lu)\n",
               (unsigned long)req->id);

        req->state = ASYNC_STATE_SENDING_REQUEST;
        /* Continue immediately to sending */
        return ASYNC_STATUS_IN_PROGRESS;
    }

    /* Check error */
    int err = SSL_get_error(req->ssl, ret);
    switch (err) {
        case SSL_ERROR_WANT_READ:
            /* Need to wait for socket to be readable */
            return ASYNC_STATUS_NEED_READ;

        case SSL_ERROR_WANT_WRITE:
            /* Need to wait for socket to be writable */
            return ASYNC_STATUS_NEED_WRITE;

        case SSL_ERROR_ZERO_RETURN:
            /* Connection closed */
            async_request_set_error(req, -1, "TLS connection closed");
            return ASYNC_STATUS_ERROR;

        default:
            /* Other error */
            {
                char err_buf[256];
                unsigned long ssl_err = ERR_get_error();
                if (ssl_err != 0) {
                    ERR_error_string_n(ssl_err, err_buf, sizeof(err_buf));
                    snprintf(req->error_msg, sizeof(req->error_msg), "TLS handshake failed: %s", err_buf);
                } else if (err == SSL_ERROR_SYSCALL) {
                    /* Get system error */
                    #ifdef _WIN32
                    int sys_err = WSAGetLastError();
                    snprintf(req->error_msg, sizeof(req->error_msg), "TLS handshake failed: system error %d (WSAERR)", sys_err);
                    #else
                    snprintf(req->error_msg, sizeof(req->error_msg), "TLS handshake failed: system error %d (errno)", errno);
                    #endif
                } else {
                    snprintf(req->error_msg, sizeof(req->error_msg), "TLS handshake failed: SSL error code %d", err);
                }
                req->state = ASYNC_STATE_ERROR;
            }
            return ASYNC_STATUS_ERROR;
    }
}

/**
 * Build HTTP request from httpmorph_request_t structure
 */
static int build_http_request(async_request_t *req) {
    httpmorph_request_t *request = req->request;

    /* Method string mapping */
    const char *method_str;
    switch (request->method) {
        case HTTPMORPH_GET:     method_str = "GET"; break;
        case HTTPMORPH_POST:    method_str = "POST"; break;
        case HTTPMORPH_PUT:     method_str = "PUT"; break;
        case HTTPMORPH_DELETE:  method_str = "DELETE"; break;
        case HTTPMORPH_HEAD:    method_str = "HEAD"; break;
        case HTTPMORPH_OPTIONS: method_str = "OPTIONS"; break;
        case HTTPMORPH_PATCH:   method_str = "PATCH"; break;
        default:               method_str = "GET"; break;
    }

    /* For HTTP through proxy, use absolute URI. For HTTPS or direct, use path only. */
    const char *request_target;
    bool use_absolute_uri = (req->proxy_host && !req->is_https);

    if (use_absolute_uri) {
        /* HTTP through proxy: use full URL as request target */
        request_target = request->url;
    } else {
        /* HTTPS through proxy or direct connection: extract path */
        const char *path = strchr(request->url, '/');
        if (path && path[0] == '/' && path[1] == '/') {
            /* Skip scheme (http:// or https://) */
            path = strchr(path + 2, '/');
        }
        if (!path || path[0] != '/') {
            path = "/";
        }
        request_target = path;
    }

    /* Build request line: METHOD request-target HTTP/1.1\r\n */
    char *buf = (char *)req->send_buf;
    int written = snprintf(buf, SEND_BUFFER_SIZE,
                          "%s %s HTTP/1.1\r\n", method_str, request_target);
    if (written < 0 || written >= (int)SEND_BUFFER_SIZE) {
        return -1;
    }

    /* Add Host header */
    written += snprintf(buf + written, SEND_BUFFER_SIZE - written,
                       "Host: %s\r\n", request->host ? request->host : "localhost");
    if (written >= (int)SEND_BUFFER_SIZE) {
        return -1;
    }

    /* Add Proxy-Authorization header for HTTP proxy (not HTTPS/CONNECT) */
    if (use_absolute_uri && (req->proxy_username || req->proxy_password)) {
        const char *username = req->proxy_username ? req->proxy_username : "";
        const char *password = req->proxy_password ? req->proxy_password : "";

        char credentials[512];
        snprintf(credentials, sizeof(credentials), "%s:%s", username, password);

        char *encoded = httpmorph_base64_encode(credentials, strlen(credentials));
        if (encoded) {
            written += snprintf(buf + written, SEND_BUFFER_SIZE - written,
                              "Proxy-Authorization: Basic %s\r\n", encoded);
            free(encoded);
            if (written >= (int)SEND_BUFFER_SIZE) {
                return -1;
            }
        }
    }

    /* Add custom headers */
    for (size_t i = 0; i < request->header_count; i++) {
        written += snprintf(buf + written, SEND_BUFFER_SIZE - written,
                           "%s: %s\r\n",
                           request->headers[i].key,
                           request->headers[i].value);
        if (written >= (int)SEND_BUFFER_SIZE) {
            return -1;
        }
    }

    /* Add Content-Length if body present */
    if (request->body && request->body_len > 0) {
        written += snprintf(buf + written, SEND_BUFFER_SIZE - written,
                           "Content-Length: %zu\r\n", request->body_len);
        if (written >= (int)SEND_BUFFER_SIZE) {
            return -1;
        }
    }

    /* End of headers */
    written += snprintf(buf + written, SEND_BUFFER_SIZE - written, "\r\n");
    if (written >= (int)SEND_BUFFER_SIZE) {
        return -1;
    }

    /* Add body if present */
    if (request->body && request->body_len > 0) {
        if (written + request->body_len >= SEND_BUFFER_SIZE) {
            return -1;  /* Body too large */
        }
        memcpy(buf + written, request->body, request->body_len);
        written += request->body_len;
    }

    req->send_len = written;
    req->send_pos = 0;

    return 0;
}

/**
 * State: Sending request
 */
static int step_sending_request(async_request_t *req) {
    /* Build request if not already done */
    if (req->send_len == 0) {
        DEBUG_PRINT("[async_request] Building HTTP request (id=%lu)\n",
               (unsigned long)req->id);

        if (build_http_request(req) < 0) {
            async_request_set_error(req, HTTPMORPH_ERROR_MEMORY,
                                   "Failed to build HTTP request");
            req->state = ASYNC_STATE_ERROR;
            return ASYNC_STATUS_ERROR;
        }
    }

    /* Send data */
    while (req->send_pos < req->send_len) {
        ssize_t sent;

        if (req->ssl) {
            /* Clear any pending errors before SSL operation */
            ERR_clear_error();

            /* SSL send - SSL layer handles non-blocking I/O internally */
            sent = SSL_write(req->ssl,
                           req->send_buf + req->send_pos,
                           (int)(req->send_len - req->send_pos));

            if (sent <= 0) {
                int err = SSL_get_error(req->ssl, (int)sent);
                if (err == SSL_ERROR_WANT_WRITE) {
                    DEBUG_PRINT("[async_request] SSL_write wants write (id=%lu)\n", (unsigned long)req->id);
                    return ASYNC_STATUS_NEED_WRITE;
                } else if (err == SSL_ERROR_WANT_READ) {
                    DEBUG_PRINT("[async_request] SSL_write wants read (id=%lu)\n", (unsigned long)req->id);
                    return ASYNC_STATUS_NEED_READ;
                } else {
                    /* Get detailed SSL error */
                    char err_buf[256];
                    unsigned long ssl_err = ERR_get_error();
                    if (ssl_err != 0) {
                        ERR_error_string_n(ssl_err, err_buf, sizeof(err_buf));
                        snprintf(req->error_msg, sizeof(req->error_msg), "SSL write failed: %s", err_buf);
                    } else if (err == SSL_ERROR_SYSCALL) {
                        /* Get system error */
                        #ifdef _WIN32
                        int sys_err = WSAGetLastError();

                        /* Handle WSAEWOULDBLOCK and WSAECONNABORTED (10053) as retriable */
                        if (sys_err == WSAEWOULDBLOCK || sys_err == 10053) {
                            if (sys_err == 10053) {
                                DEBUG_PRINT("[async_request] SSL_write got WSAECONNABORTED, retrying (id=%lu)\n", (unsigned long)req->id);
                            } else {
                                DEBUG_PRINT("[async_request] SSL_write got WSAEWOULDBLOCK, retrying (id=%lu)\n", (unsigned long)req->id);
                            }
                            return ASYNC_STATUS_NEED_WRITE;
                        }

                        /* Other errors */
                        snprintf(req->error_msg, sizeof(req->error_msg), "SSL write failed: system error %d (WSAERR)", sys_err);
                        #else
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            return ASYNC_STATUS_NEED_WRITE;
                        }
                        snprintf(req->error_msg, sizeof(req->error_msg), "SSL write failed: system error %d (errno)", errno);
                        #endif
                    } else {
                        snprintf(req->error_msg, sizeof(req->error_msg), "SSL write failed: error code %d", err);
                    }
                    req->state = ASYNC_STATE_ERROR;
                    req->error_code = err;
                    return ASYNC_STATUS_ERROR;
                }
            }

            DEBUG_PRINT("[async_request] SSL_write sent %zd bytes (id=%lu)\n", sent, (unsigned long)req->id);
            req->send_pos += sent;
        } else {
#ifdef _WIN32
            /* Windows: Skip IOCP for async requests - use regular non-blocking I/O */
            if (false && req->io_engine && req->io_engine->type == IO_ENGINE_IOCP) {
                /* Check if previous operation completed */
                if (req->iocp_operation_pending && req->overlapped_send) {
                    OVERLAPPED *ov = (OVERLAPPED*)req->overlapped_send;
                    DWORD bytes_transferred = 0;
                    DWORD flags = 0;

                    if (WSAGetOverlappedResult(req->sockfd, ov, &bytes_transferred, FALSE, &flags)) {
                        /* Send completed */
                        req->iocp_operation_pending = false;
                        req->send_pos += bytes_transferred;
                        DEBUG_PRINT("[async_request] WSASend completed: %lu bytes (id=%lu)\n",
                               (unsigned long)bytes_transferred, (unsigned long)req->id);
                        continue;  /* Try to send more */
                    } else {
                        req->iocp_last_error = WSAGetLastError();
                        if (req->iocp_last_error == WSA_IO_INCOMPLETE) {
                            /* Still pending */
                            return ASYNC_STATUS_NEED_WRITE;
                        } else {
                            /* Send failed */
                            char error_buf[256];
                            snprintf(error_buf, sizeof(error_buf), "WSASend failed: %d", req->iocp_last_error);
                            async_request_set_error(req, req->iocp_last_error, error_buf);
                            return ASYNC_STATUS_ERROR;
                        }
                    }
                }

                /* Start new WSASend operation */
                if (!req->overlapped_send) {
                    req->overlapped_send = alloc_overlapped();
                    if (!req->overlapped_send) {
                        async_request_set_error(req, -1, "Failed to allocate OVERLAPPED for send");
                        return ASYNC_STATUS_ERROR;
                    }
                }

                /* Reset OVERLAPPED structure */
                OVERLAPPED *ov = (OVERLAPPED*)req->overlapped_send;
                memset(ov, 0, sizeof(OVERLAPPED));

                /* Reset completion event before starting new operation */
                ResetEvent((HANDLE)req->iocp_completion_event);

                WSABUF buf;
                buf.buf = (char*)(req->send_buf + req->send_pos);
                buf.len = req->send_len - req->send_pos;

                DWORD bytes_sent = 0;
                int result = WSASend(req->sockfd, &buf, 1, &bytes_sent, 0, ov, NULL);

                if (result == 0) {
                    /* Completed immediately */
                    req->send_pos += bytes_sent;
                    DEBUG_PRINT("[async_request] WSASend completed immediately: %lu bytes (id=%lu)\n",
                           (unsigned long)bytes_sent, (unsigned long)req->id);
                    continue;  /* Try to send more */
                } else {
                    req->iocp_last_error = WSAGetLastError();
                    if (req->iocp_last_error == WSA_IO_PENDING) {
                        /* Async operation started */
                        req->iocp_operation_pending = true;
                        DEBUG_PRINT("[async_request] WSASend pending (id=%lu)\n", (unsigned long)req->id);
                        return ASYNC_STATUS_NEED_WRITE;
                    } else {
                        /* Send failed */
                        char error_buf[256];
                        snprintf(error_buf, sizeof(error_buf), "WSASend failed: %d", req->iocp_last_error);
                        async_request_set_error(req, req->iocp_last_error, error_buf);
                        return ASYNC_STATUS_ERROR;
                    }
                }
            } else
#endif
            {
                /* Plain TCP send (non-IOCP) */
                sent = send(req->sockfd,
                           (const char*)(req->send_buf + req->send_pos),
                           req->send_len - req->send_pos,
                           0);

                if (sent < 0) {
#ifdef _WIN32
                    int err = WSAGetLastError();
                    if (err == WSAEWOULDBLOCK) {
                        return ASYNC_STATUS_NEED_WRITE;
                    }
#else
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        return ASYNC_STATUS_NEED_WRITE;
                    }
#endif
                    async_request_set_error(req, errno, "Send failed");
                    return ASYNC_STATUS_ERROR;
                }

                if (sent == 0) {
                    async_request_set_error(req, -1, "Connection closed");
                    return ASYNC_STATUS_ERROR;
                }

                req->send_pos += sent;
            }
        }
    }

    /* All data sent */
    DEBUG_PRINT("[async_request] Request sent (%zu bytes) (id=%lu)\n",
           req->send_len, (unsigned long)req->id);

    req->state = ASYNC_STATE_RECEIVING_HEADERS;
    return ASYNC_STATUS_IN_PROGRESS;
}

/**
 * State: Receiving headers
 */
static int step_receiving_headers(async_request_t *req) {
    /* Receive data */
    ssize_t received;

    if (req->ssl) {
        /* SSL receive - SSL layer handles non-blocking I/O internally */
        received = SSL_read(req->ssl,
                          req->recv_buf + req->recv_len,
                          (int)(req->recv_capacity - req->recv_len));

        if (received <= 0) {
            int err = SSL_get_error(req->ssl, (int)received);
            if (err == SSL_ERROR_WANT_READ) {
                return ASYNC_STATUS_NEED_READ;
            } else if (err == SSL_ERROR_WANT_WRITE) {
                return ASYNC_STATUS_NEED_WRITE;
            } else if (err == SSL_ERROR_ZERO_RETURN) {
                /* SSL connection closed - check if we have complete headers */
                if (req->recv_len >= 4) {
                    bool has_complete_headers = false;
                    for (size_t i = 0; i <= req->recv_len - 4; i++) {
                        if (memcmp(req->recv_buf + i, "\r\n\r\n", 4) == 0) {
                            has_complete_headers = true;
                            break;
                        }
                    }
                    if (has_complete_headers) {
                        /* Have complete headers, process them */
                        received = 0;  /* Set to 0 to skip recv_len increment below */
                        goto ssl_process_headers;
                    }
                }
                async_request_set_error(req, -1, "SSL connection closed before complete headers");
                return ASYNC_STATUS_ERROR;
            } else {
                /* Get detailed SSL error */
                char err_buf[256];
                unsigned long ssl_err = ERR_get_error();
                if (ssl_err != 0) {
                    ERR_error_string_n(ssl_err, err_buf, sizeof(err_buf));
                    snprintf(req->error_msg, sizeof(req->error_msg), "SSL read failed: %s", err_buf);
                } else if (err == SSL_ERROR_SYSCALL) {
                    /* Get system error */
                    #ifdef _WIN32
                    int sys_err = WSAGetLastError();
                    snprintf(req->error_msg, sizeof(req->error_msg), "SSL read failed: system error %d (WSAERR)", sys_err);
                    #else
                    snprintf(req->error_msg, sizeof(req->error_msg), "SSL read failed: system error %d (errno)", errno);
                    #endif
                } else {
                    snprintf(req->error_msg, sizeof(req->error_msg), "SSL read failed: error code %d", err);
                }
                req->state = ASYNC_STATE_ERROR;
                req->error_code = err;
                return ASYNC_STATUS_ERROR;
            }
        }

ssl_process_headers:
        /* Increment recv_len for SSL path (only if received > 0) */
        if (received > 0) {
            req->recv_len += received;
        }
    } else {
#ifdef _WIN32
        /* Windows: Skip IOCP for async requests - use regular non-blocking I/O instead */
        /* IOCP has issues with immediate connection close scenarios */
        if (false && req->io_engine && req->io_engine->type == IO_ENGINE_IOCP) {
            /* Check if previous operation completed (only if we started one) */
            if (req->overlapped_recv) {
                /* Check if completion event was signaled */
                DWORD wait_result = WaitForSingleObject((HANDLE)req->iocp_completion_event, 0);

                if (wait_result == WAIT_OBJECT_0) {
                    /* Operation completed */
                    DEBUG_PRINT("[async_request] WSARecv (headers) completed via dispatcher (error=%d, bytes=%lu, id=%lu)\n",
                           req->iocp_last_error, (unsigned long)req->iocp_bytes_transferred, (unsigned long)req->id);

                    /* Reset event for next operation */
                    ResetEvent((HANDLE)req->iocp_completion_event);

                    if (req->iocp_last_error == 0) {
                        /* Receive successful */
                        received = req->iocp_bytes_transferred;

                        /* Free and clear overlapped so a new operation can be started */
                        free_overlapped(req->overlapped_recv);
                        req->overlapped_recv = NULL;

                        if (received == 0) {
                            /* Connection closed - check if we have complete headers already */
                            if (req->recv_len >= 4) {
                                bool has_complete_headers = false;
                                for (size_t i = 0; i <= req->recv_len - 4; i++) {
                                    if (memcmp(req->recv_buf + i, "\r\n\r\n", 4) == 0) {
                                        has_complete_headers = true;
                                        break;
                                    }
                                }
                                if (has_complete_headers) {
                                    /* Have complete headers, don't increment recv_len, continue to header processing */
                                    goto iocp_process_headers;
                                }
                            }
                            async_request_set_error(req, -1, "Connection closed before complete headers (IOCP)");
                            return ASYNC_STATUS_ERROR;
                        }
                    } else {
                        /* Receive failed */
                        char error_buf[256];
                        snprintf(error_buf, sizeof(error_buf), "WSARecv failed: %d", req->iocp_last_error);
                        async_request_set_error(req, req->iocp_last_error, error_buf);
                        return ASYNC_STATUS_ERROR;
                    }
                } else if (wait_result == WAIT_TIMEOUT) {
                    /* Still pending */
                    return ASYNC_STATUS_NEED_READ;
                } else {
                    /* Wait failed */
                    req->iocp_last_error = GetLastError();
                    char error_buf[256];
                    snprintf(error_buf, sizeof(error_buf), "WaitForSingleObject failed: %d", req->iocp_last_error);
                    async_request_set_error(req, req->iocp_last_error, error_buf);
                    return ASYNC_STATUS_ERROR;
                }
            } else {
                /* Start new WSARecv operation */
                if (!req->overlapped_recv) {
                    req->overlapped_recv = alloc_overlapped();
                    if (!req->overlapped_recv) {
                        async_request_set_error(req, -1, "Failed to allocate OVERLAPPED for recv");
                        return ASYNC_STATUS_ERROR;
                    }
                }

                /* Reset OVERLAPPED structure */
                OVERLAPPED *ov = (OVERLAPPED*)req->overlapped_recv;
                memset(ov, 0, sizeof(OVERLAPPED));

                /* Reset completion event before starting new operation */
                ResetEvent((HANDLE)req->iocp_completion_event);

                WSABUF buf;
                buf.buf = (char*)(req->recv_buf + req->recv_len);
                buf.len = req->recv_capacity - req->recv_len;

                DWORD bytes_received = 0;
                DWORD flags = 0;
                int result = WSARecv(req->sockfd, &buf, 1, &bytes_received, &flags, ov, NULL);

                if (result == 0) {
                    /* Completed immediately */
                    received = bytes_received;
                    DEBUG_PRINT("[async_request] WSARecv completed immediately: %lu bytes (id=%lu)\n",
                           (unsigned long)bytes_received, (unsigned long)req->id);

                    if (received == 0) {
                        /* Connection closed (or closing) - check if we have complete headers already */
                        if (req->recv_len >= 4) {
                            bool has_complete_headers = false;
                            for (size_t i = 0; i <= req->recv_len - 4; i++) {
                                if (memcmp(req->recv_buf + i, "\r\n\r\n", 4) == 0) {
                                    has_complete_headers = true;
                                    break;
                                }
                            }
                            if (has_complete_headers) {
                                /* Have complete headers, continue to header processing */
                                goto iocp_process_headers;
                            }
                        }
                        /* If this is the first receive (recv_len == 0), give it another chance */
                        /* Server might be closing connection but data could still arrive */
                        if (req->recv_len == 0) {
                            DEBUG_PRINT("[async_request] WSARecv got 0 bytes on first recv, will retry (id=%lu)\n", (unsigned long)req->id);
                            return ASYNC_STATUS_NEED_READ;
                        }
                        async_request_set_error(req, -1, "Connection closed before complete headers (WSARecv immediate)");
                        return ASYNC_STATUS_ERROR;
                    }
                } else {
                    req->iocp_last_error = WSAGetLastError();
                    if (req->iocp_last_error == WSA_IO_PENDING) {
                        /* Async operation started */
                        req->iocp_operation_pending = true;
                        DEBUG_PRINT("[async_request] WSARecv pending (id=%lu)\n", (unsigned long)req->id);
                        return ASYNC_STATUS_NEED_READ;
                    } else {
                        /* Receive failed */
                        char error_buf[256];
                        snprintf(error_buf, sizeof(error_buf), "WSARecv failed: %d", req->iocp_last_error);
                        async_request_set_error(req, req->iocp_last_error, error_buf);
                        return ASYNC_STATUS_ERROR;
                    }
                }
            }
        } else
#endif
        {
            /* Plain TCP receive (non-IOCP) */
            received = recv(req->sockfd,
                           (char*)(req->recv_buf + req->recv_len),
                           req->recv_capacity - req->recv_len,
                           0);

            if (received < 0) {
#ifdef _WIN32
                int err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK) {
                    return ASYNC_STATUS_NEED_READ;
                }
#else
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return ASYNC_STATUS_NEED_READ;
                }
#endif
                async_request_set_error(req, errno, "Receive failed");
                return ASYNC_STATUS_ERROR;
            }

            if (received == 0) {
                /* Connection closed - this is only an error if we don't have complete headers */
                /* If we have complete headers, we'll process them below and handle body separately */
                if (req->recv_len < 4) {
                    /* Not enough data for headers */
                    async_request_set_error(req, -1, "Connection closed before headers");
                    return ASYNC_STATUS_ERROR;
                }
                /* Check if we have complete headers */
                bool has_complete_headers = false;
                for (size_t i = 0; i <= req->recv_len - 4; i++) {
                    if (memcmp(req->recv_buf + i, "\r\n\r\n", 4) == 0) {
                        has_complete_headers = true;
                        break;
                    }
                }
                if (!has_complete_headers) {
                    async_request_set_error(req, -1, "Connection closed before complete headers");
                    return ASYNC_STATUS_ERROR;
                }
                /* We have complete headers, continue processing below */
                /* Don't increment recv_len since we didn't actually receive any new data */
            } else {
                /* Normal receive, add to buffer */
                req->recv_len += received;
            }
        }
    }

iocp_process_headers:
    /* Look for end of headers (\r\n\r\n) */
    if (req->recv_len >= 4) {
        for (size_t i = 0; i <= req->recv_len - 4; i++) {
            if (memcmp(req->recv_buf + i, "\r\n\r\n", 4) == 0) {
                /* Found end of headers */
                req->headers_complete = true;
                req->headers_end_pos = i + 4;

                DEBUG_PRINT("[async_request] Headers received (%zu bytes) (id=%lu)\n",
                       req->headers_end_pos, (unsigned long)req->id);

                /* Parse headers for Content-Length and Transfer-Encoding */
                req->content_length = 0;
                req->chunked_encoding = false;

                /* Search for Content-Length header (case-insensitive) */
                char *headers_start = (char *)req->recv_buf;
                char *headers_end = headers_start + req->headers_end_pos;

                /* Look for "Content-Length:" */
                for (char *p = headers_start; p < headers_end - 16; p++) {
                    if ((p[0] == 'C' || p[0] == 'c') &&
                        (p[1] == 'o' || p[1] == 'O') &&
                        (p[2] == 'n' || p[2] == 'N') &&
                        (p[3] == 't' || p[3] == 'T') &&
                        (p[4] == 'e' || p[4] == 'E') &&
                        (p[5] == 'n' || p[5] == 'N') &&
                        (p[6] == 't' || p[6] == 'T') &&
                        p[7] == '-' &&
                        (p[8] == 'L' || p[8] == 'l') &&
                        (p[9] == 'e' || p[9] == 'E') &&
                        (p[10] == 'n' || p[10] == 'N') &&
                        (p[11] == 'g' || p[11] == 'G') &&
                        (p[12] == 't' || p[12] == 'T') &&
                        (p[13] == 'h' || p[13] == 'H') &&
                        p[14] == ':') {
                        /* Found Content-Length header */
                        p += 15;
                        /* Skip whitespace */
                        while (p < headers_end && (*p == ' ' || *p == '\t')) p++;
                        /* Parse number */
                        req->content_length = 0;
                        while (p < headers_end && *p >= '0' && *p <= '9') {
                            req->content_length = req->content_length * 10 + (*p - '0');
                            p++;
                        }
                        break;
                    }
                }

                /* Look for "Transfer-Encoding: chunked" */
                for (char *p = headers_start; p < headers_end - 25; p++) {
                    if ((p[0] == 'T' || p[0] == 't') &&
                        (p[1] == 'r' || p[1] == 'R') &&
                        (p[2] == 'a' || p[2] == 'A') &&
                        (p[3] == 'n' || p[3] == 'N') &&
                        (p[4] == 's' || p[4] == 'S') &&
                        (p[5] == 'f' || p[5] == 'F') &&
                        (p[6] == 'e' || p[6] == 'E') &&
                        (p[7] == 'r' || p[7] == 'R') &&
                        p[8] == '-' &&
                        (p[9] == 'E' || p[9] == 'e') &&
                        (p[10] == 'n' || p[10] == 'N') &&
                        (p[11] == 'c' || p[11] == 'C') &&
                        (p[12] == 'o' || p[12] == 'O') &&
                        (p[13] == 'd' || p[13] == 'D') &&
                        (p[14] == 'i' || p[14] == 'I') &&
                        (p[15] == 'n' || p[15] == 'N') &&
                        (p[16] == 'g' || p[16] == 'G') &&
                        p[17] == ':') {
                        /* Found Transfer-Encoding header, check if chunked */
                        p += 18;
                        /* Skip whitespace */
                        while (p < headers_end && (*p == ' ' || *p == '\t')) p++;
                        /* Check for "chunked" */
                        if (p < headers_end - 7 &&
                            (p[0] == 'c' || p[0] == 'C') &&
                            (p[1] == 'h' || p[1] == 'H') &&
                            (p[2] == 'u' || p[2] == 'U') &&
                            (p[3] == 'n' || p[3] == 'N') &&
                            (p[4] == 'k' || p[4] == 'K') &&
                            (p[5] == 'e' || p[5] == 'E') &&
                            (p[6] == 'd' || p[6] == 'D')) {
                            req->chunked_encoding = true;
                        }
                        break;
                    }
                }

                DEBUG_PRINT("[async_request] Content-Length: %zu, Chunked: %d (id=%lu)\n",
                       req->content_length, req->chunked_encoding, (unsigned long)req->id);

                /* Check if we already have body data in the buffer */
                size_t body_start = req->headers_end_pos;
                if (body_start < req->recv_len) {
                    /* We have some body data already */
                    req->body_received = req->recv_len - body_start;
                    DEBUG_PRINT("[async_request] Already received %zu bytes of body with headers (id=%lu)\n",
                           req->body_received, (unsigned long)req->id);
                }

                req->state = ASYNC_STATE_RECEIVING_BODY;
                return ASYNC_STATUS_IN_PROGRESS;
            }
        }
    }

    /* Need more data */
    return ASYNC_STATUS_NEED_READ;
}

/**
 * State: Receiving body
 */
static int step_receiving_body(async_request_t *req) {
    /* If no body expected, complete immediately */
    if (req->content_length == 0 && !req->chunked_encoding) {
        DEBUG_PRINT("[async_request] No body to receive (id=%lu)\n",
               (unsigned long)req->id);

        /* Create response object for empty body */
        if (!req->response) {
            req->response = calloc(1, sizeof(httpmorph_response_t));
            if (req->response) {
                req->response->body = NULL;
                req->response->body_len = 0;
                req->response->status_code = 200;  /* TODO: Parse from headers */
                req->response->http_version = HTTPMORPH_VERSION_1_1;
                req->response->error = HTTPMORPH_OK;
            }
        }

        req->state = ASYNC_STATE_COMPLETE;
        return ASYNC_STATUS_COMPLETE;
    }

    /* Check if we already have all the body data */
    if (!req->chunked_encoding && req->body_received >= req->content_length) {
        DEBUG_PRINT("[async_request] Body already complete (%zu bytes) (id=%lu)\n",
               req->body_received, (unsigned long)req->id);

        /* Create response object */
        if (!req->response) {
            req->response = calloc(1, sizeof(httpmorph_response_t));
            if (req->response) {
                /* Extract body from recv_buf (starts after headers) */
                size_t body_start = req->headers_end_pos;
                if (req->content_length > 0) {
                    req->response->body = malloc(req->content_length);
                    if (req->response->body) {
                        memcpy(req->response->body, req->recv_buf + body_start, req->content_length);
                        req->response->body_len = req->content_length;
                    }
                }
                req->response->status_code = 200;  // TODO: Parse from headers
                req->response->http_version = HTTPMORPH_VERSION_1_1;
                req->response->error = HTTPMORPH_OK;
            }
        }

        req->state = ASYNC_STATE_COMPLETE;
        return ASYNC_STATUS_COMPLETE;
    }

    /* Receive body data */
    ssize_t received;

    if (req->ssl) {
        /* SSL receive - SSL layer handles non-blocking I/O internally */
        received = SSL_read(req->ssl,
                          req->recv_buf + req->recv_len,
                          (int)(req->recv_capacity - req->recv_len));

        if (received <= 0) {
            int err = SSL_get_error(req->ssl, (int)received);
            if (err == SSL_ERROR_WANT_READ) {
                return ASYNC_STATUS_NEED_READ;
            } else if (err == SSL_ERROR_WANT_WRITE) {
                return ASYNC_STATUS_NEED_WRITE;
            } else if (err == SSL_ERROR_ZERO_RETURN) {
                /* Connection closed - check if we got all data */
                if (req->content_length > 0 &&
                    req->body_received < req->content_length) {
                    async_request_set_error(req, -1, "Incomplete body");
                    return ASYNC_STATUS_ERROR;
                }
                req->state = ASYNC_STATE_COMPLETE;
                return ASYNC_STATUS_COMPLETE;
            } else {
                async_request_set_error(req, err, "SSL read failed");
                return ASYNC_STATUS_ERROR;
            }
        }
    } else {
#ifdef _WIN32
        /* Windows: Skip IOCP for async requests - use regular non-blocking I/O instead */
        /* IOCP has issues with immediate connection close scenarios */
        if (false && req->io_engine && req->io_engine->type == IO_ENGINE_IOCP) {
            /* Check if previous operation completed */
            if (req->overlapped_recv) {
                /* Check if completion event was signaled */
                DWORD wait_result = WaitForSingleObject((HANDLE)req->iocp_completion_event, 0);

                if (wait_result == WAIT_OBJECT_0) {
                    /* Operation completed */
                    req->iocp_operation_pending = false;

                    if (req->iocp_last_error == 0) {
                        /* Receive successful */
                        received = req->iocp_bytes_transferred;
                        DEBUG_PRINT("[async_request] WSARecv (body) completed via dispatcher: %lu bytes (id=%lu)\n",
                               (unsigned long)received, (unsigned long)req->id);

                        /* Reset event for next operation */
                        ResetEvent((HANDLE)req->iocp_completion_event);

                        /* Free and clear overlapped so a new operation can be started */
                        free_overlapped(req->overlapped_recv);
                        req->overlapped_recv = NULL;

                        if (received == 0) {
                            /* Connection closed - check if we got all data */
                            if (req->content_length > 0 && req->body_received < req->content_length) {
                                async_request_set_error(req, -1, "Incomplete body");
                                return ASYNC_STATUS_ERROR;
                            }
                            req->state = ASYNC_STATE_COMPLETE;
                            return ASYNC_STATUS_COMPLETE;
                        }
                    } else {
                        /* Receive failed */
                        char error_buf[256];
                        snprintf(error_buf, sizeof(error_buf), "WSARecv (body) failed: %d", req->iocp_last_error);
                        async_request_set_error(req, req->iocp_last_error, error_buf);
                        return ASYNC_STATUS_ERROR;
                    }
                } else if (wait_result == WAIT_TIMEOUT) {
                    /* Still pending */
                    return ASYNC_STATUS_NEED_READ;
                } else {
                    /* Wait failed */
                    req->iocp_last_error = GetLastError();
                    char error_buf[256];
                    snprintf(error_buf, sizeof(error_buf), "WaitForSingleObject failed: %d", req->iocp_last_error);
                    async_request_set_error(req, req->iocp_last_error, error_buf);
                    return ASYNC_STATUS_ERROR;
                }
            } else {
                /* Start new WSARecv operation */
                if (!req->overlapped_recv) {
                    req->overlapped_recv = alloc_overlapped();
                    if (!req->overlapped_recv) {
                        async_request_set_error(req, -1, "Failed to allocate OVERLAPPED for recv");
                        return ASYNC_STATUS_ERROR;
                    }
                }

                /* Reset OVERLAPPED structure */
                OVERLAPPED *ov = (OVERLAPPED*)req->overlapped_recv;
                memset(ov, 0, sizeof(OVERLAPPED));

                /* Reset completion event before starting new operation */
                ResetEvent((HANDLE)req->iocp_completion_event);

                WSABUF buf;
                buf.buf = (char*)(req->recv_buf + req->recv_len);
                buf.len = req->recv_capacity - req->recv_len;

                DWORD bytes_received = 0;
                DWORD flags = 0;
                int result = WSARecv(req->sockfd, &buf, 1, &bytes_received, &flags, ov, NULL);

                if (result == 0) {
                    /* Completed immediately */
                    received = bytes_received;
                    DEBUG_PRINT("[async_request] WSARecv (body) completed immediately: %lu bytes (id=%lu)\n",
                           (unsigned long)bytes_received, (unsigned long)req->id);

                    if (received == 0) {
                        /* Connection closed - check if we got all data */
                        if (req->content_length > 0 && req->body_received < req->content_length) {
                            async_request_set_error(req, -1, "Incomplete body");
                            return ASYNC_STATUS_ERROR;
                        }
                        req->state = ASYNC_STATE_COMPLETE;
                        return ASYNC_STATUS_COMPLETE;
                    }
                } else {
                    req->iocp_last_error = WSAGetLastError();
                    if (req->iocp_last_error == WSA_IO_PENDING) {
                        /* Async operation started */
                        req->iocp_operation_pending = true;
                        DEBUG_PRINT("[async_request] WSARecv (body) pending (id=%lu)\n", (unsigned long)req->id);
                        return ASYNC_STATUS_NEED_READ;
                    } else {
                        /* Receive failed */
                        char error_buf[256];
                        snprintf(error_buf, sizeof(error_buf), "WSARecv (body) failed: %d", req->iocp_last_error);
                        async_request_set_error(req, req->iocp_last_error, error_buf);
                        return ASYNC_STATUS_ERROR;
                    }
                }
            }
        } else
#endif
        {
            /* Plain TCP receive (non-IOCP) */
            received = recv(req->sockfd,
                           (char*)(req->recv_buf + req->recv_len),
                           req->recv_capacity - req->recv_len,
                           0);

            if (received < 0) {
#ifdef _WIN32
                int err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK) {
                    return ASYNC_STATUS_NEED_READ;
                }
#else
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return ASYNC_STATUS_NEED_READ;
                }
#endif
                async_request_set_error(req, errno, "Receive failed");
                return ASYNC_STATUS_ERROR;
            }

            if (received == 0) {
                /* Connection closed - check if we got all data */
                if (req->content_length > 0 &&
                    req->body_received < req->content_length) {
                    async_request_set_error(req, -1, "Incomplete body");
                    return ASYNC_STATUS_ERROR;
                }
                req->state = ASYNC_STATE_COMPLETE;
                return ASYNC_STATUS_COMPLETE;
            }
        }
    }

    req->recv_len += received;
    req->body_received += received;

    /* Check if we received all data */
    if (req->content_length > 0 && req->body_received >= req->content_length) {
        DEBUG_PRINT("[async_request] Body received (%zu bytes) (id=%lu)\n",
               req->body_received, (unsigned long)req->id);

        /* Create response object */
        if (!req->response) {
            req->response = calloc(1, sizeof(httpmorph_response_t));
            if (req->response) {
                /* Extract body from recv_buf (starts after headers) */
                size_t body_start = req->headers_end_pos;
                if (req->content_length > 0) {
                    req->response->body = malloc(req->content_length);
                    if (req->response->body) {
                        memcpy(req->response->body, req->recv_buf + body_start, req->content_length);
                        req->response->body_len = req->content_length;
                    }
                }
                req->response->status_code = 200;  // TODO: Parse from headers
                req->response->http_version = HTTPMORPH_VERSION_1_1;
                req->response->error = HTTPMORPH_OK;
            }
        }

        req->state = ASYNC_STATE_COMPLETE;
        return ASYNC_STATUS_COMPLETE;
    }

    /* Need more data */
    return ASYNC_STATUS_NEED_READ;
}

/**
 * Handle proxy CONNECT state (establish tunnel through proxy)
 */
static int step_proxy_connect(async_request_t *req) {
    /* Send CONNECT request if not already sent */
    if (!req->proxy_connect_sent) {
        /* Build CONNECT request */
        char connect_req[2048];
        int len = snprintf(connect_req, sizeof(connect_req),
                          "CONNECT %s:%u HTTP/1.1\r\n"
                          "Host: %s:%u\r\n",
                          req->target_host, req->target_port,
                          req->target_host, req->target_port);

        /* Add Proxy-Authorization if credentials provided */
        if (req->proxy_username && req->proxy_password) {
            char credentials[512];
            snprintf(credentials, sizeof(credentials), "%s:%s",
                    req->proxy_username, req->proxy_password);

            /* Base64 encode credentials */
            extern char* httpmorph_base64_encode(const char *data, size_t len);
            char *encoded = httpmorph_base64_encode(credentials, strlen(credentials));
            if (encoded) {
                len += snprintf(connect_req + len, sizeof(connect_req) - len,
                              "Proxy-Authorization: Basic %s\r\n", encoded);
                free(encoded);
            }
        }

        /* End headers */
        len += snprintf(connect_req + len, sizeof(connect_req) - len, "\r\n");

        /* Send CONNECT request (non-blocking) */
        ssize_t sent;
#ifdef _WIN32
        sent = send(req->sockfd, connect_req, len, 0);
        if (sent < 0 && WSAGetLastError() == WSAEWOULDBLOCK) {
            return ASYNC_STATUS_NEED_WRITE;
        }
#else
        sent = send(req->sockfd, connect_req, len, 0);
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOTCONN)) {
            /* Socket not ready yet - wait and try again */
            return ASYNC_STATUS_NEED_WRITE;
        }
#endif

        if (sent < 0) {
            char error_buf[256];
            snprintf(error_buf, sizeof(error_buf), "Failed to send CONNECT request to proxy: %s (errno=%d)",
                    strerror(errno), errno);
            async_request_set_error(req, errno, error_buf);
            return ASYNC_STATUS_ERROR;
        }

        if ((size_t)sent < (size_t)len) {
            async_request_set_error(req, -1, "Partial CONNECT send (not supported yet)");
            return ASYNC_STATUS_ERROR;
        }

        req->proxy_connect_sent = true;
        DEBUG_PRINT("[async_request] Sent CONNECT to proxy %s:%u for target %s:%u (id=%lu)\n",
               req->proxy_host, req->proxy_port,
               req->target_host, req->target_port,
               (unsigned long)req->id);
    }

    /* Receive CONNECT response (non-blocking) */
    ssize_t received;
#ifdef _WIN32
    received = recv(req->sockfd, (char*)req->proxy_recv_buf + req->proxy_recv_len,
                   4096 - req->proxy_recv_len - 1, 0);
    if (received < 0 && WSAGetLastError() == WSAEWOULDBLOCK) {
        return ASYNC_STATUS_NEED_READ;
    }
#else
    received = recv(req->sockfd, req->proxy_recv_buf + req->proxy_recv_len,
                   4096 - req->proxy_recv_len - 1, 0);
    if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return ASYNC_STATUS_NEED_READ;
    }
#endif

    if (received < 0) {
        async_request_set_error(req, errno, "Failed to receive CONNECT response from proxy");
        return ASYNC_STATUS_ERROR;
    }

    if (received == 0) {
        async_request_set_error(req, -1, "Proxy closed connection during CONNECT");
        return ASYNC_STATUS_ERROR;
    }

    req->proxy_recv_len += received;
    req->proxy_recv_buf[req->proxy_recv_len] = '\0';

    /* Check if we have complete response (look for \r\n\r\n) */
    if (strstr((char*)req->proxy_recv_buf, "\r\n\r\n") == NULL) {
        /* Need more data */
        if (req->proxy_recv_len >= 4000) {
            async_request_set_error(req, -1, "Proxy CONNECT response too large");
            return ASYNC_STATUS_ERROR;
        }
        return ASYNC_STATUS_NEED_READ;
    }

    /* Parse response: expect "HTTP/1.x 200" */
    if (strncmp((char*)req->proxy_recv_buf, "HTTP/1.1 200", 12) != 0 &&
        strncmp((char*)req->proxy_recv_buf, "HTTP/1.0 200", 12) != 0) {
        /* Extract status code for error message */
        char status_msg[256];
        char *newline = strchr((char*)req->proxy_recv_buf, '\r');
        if (newline) {
            size_t status_len = newline - (char*)req->proxy_recv_buf;
            if (status_len > sizeof(status_msg) - 1) {
                status_len = sizeof(status_msg) - 1;
            }
            memcpy(status_msg, req->proxy_recv_buf, status_len);
            status_msg[status_len] = '\0';
            async_request_set_error(req, -1, status_msg);
        } else {
            async_request_set_error(req, -1, "Proxy CONNECT failed (invalid response)");
        }
        return ASYNC_STATUS_ERROR;
    }

    DEBUG_PRINT("[async_request] Proxy CONNECT succeeded, tunnel established (id=%lu)\n",
           (unsigned long)req->id);

    /* Proxy tunnel established, proceed to TLS if target uses HTTPS, otherwise send request */
    if (req->is_https) {
        req->state = ASYNC_STATE_TLS_HANDSHAKE;
    } else {
        req->state = ASYNC_STATE_SENDING_REQUEST;
    }

    return ASYNC_STATUS_IN_PROGRESS;
}

/**
 * Step the async request state machine
 */
int async_request_step(async_request_t *req) {
    if (!req) {
        return ASYNC_STATUS_ERROR;
    }

    /* Check timeout */
    if (async_request_is_timeout(req)) {
        async_request_set_error(req, -1, "Request timeout");
        if (req->on_complete) {
            req->on_complete(req, ASYNC_STATUS_ERROR);
        }
        return ASYNC_STATUS_ERROR;
    }

    /* State machine */
    switch (req->state) {
        case ASYNC_STATE_INIT:
            /* Start with DNS lookup */
            req->state = ASYNC_STATE_DNS_LOOKUP;
            return ASYNC_STATUS_IN_PROGRESS;

        case ASYNC_STATE_DNS_LOOKUP:
            return step_dns_lookup(req);

        case ASYNC_STATE_CONNECTING:
            return step_connecting(req);

        case ASYNC_STATE_PROXY_CONNECT:
            return step_proxy_connect(req);

        case ASYNC_STATE_TLS_HANDSHAKE:
            return step_tls_handshake(req);

        case ASYNC_STATE_SENDING_REQUEST:
            return step_sending_request(req);

        case ASYNC_STATE_RECEIVING_HEADERS:
            return step_receiving_headers(req);

        case ASYNC_STATE_RECEIVING_BODY:
            return step_receiving_body(req);

        case ASYNC_STATE_COMPLETE:
            /* Already complete */
            if (req->on_complete) {
                req->on_complete(req, ASYNC_STATUS_COMPLETE);
            }
            return ASYNC_STATUS_COMPLETE;

        case ASYNC_STATE_ERROR:
            /* Already in error state */
            if (req->on_complete) {
                req->on_complete(req, ASYNC_STATUS_ERROR);
            }
            return ASYNC_STATUS_ERROR;

        default:
            async_request_set_error(req, -1, "Invalid state");
            return ASYNC_STATUS_ERROR;
    }
}
