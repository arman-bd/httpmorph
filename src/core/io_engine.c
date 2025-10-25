/**
 * io_engine.c - Implementation of high-performance I/O engine
 */

#include "io_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>  /* for PRIu64 */

/* Platform-specific headers */
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #define close closesocket
#else
    #include <unistd.h>
    #include <errno.h>
    #include <fcntl.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
#endif

#ifdef __linux__
#include <sys/epoll.h>
#endif

#ifdef __APPLE__
#include <sys/event.h>
#include <sys/time.h>
#endif

#ifdef HAVE_IO_URING
#include <liburing.h>
#endif

/* Default queue depth for io_uring */
#define DEFAULT_QUEUE_DEPTH 256

/* Maximum events to process at once */
#define MAX_EVENTS 128

/**
 * Check if io_uring is available
 */
bool io_engine_has_uring(void) {
#ifdef HAVE_IO_URING
    struct io_uring ring;
    if (io_uring_queue_init(2, &ring, 0) == 0) {
        io_uring_queue_exit(&ring);
        return true;
    }
#endif
    return false;
}

/**
 * Get engine type name
 */
const char* io_engine_type_name(io_engine_type_t type) {
    switch (type) {
        case IO_ENGINE_URING:  return "io_uring";
        case IO_ENGINE_EPOLL:  return "epoll";
        case IO_ENGINE_KQUEUE: return "kqueue";
        default:               return "unknown";
    }
}

/**
 * Create epoll-based engine (Linux only)
 */
static io_engine_t* io_engine_create_epoll(void) {
#ifdef __linux__
    io_engine_t *engine = calloc(1, sizeof(io_engine_t));
    if (!engine) {
        return NULL;
    }

    engine->type = IO_ENGINE_EPOLL;
    engine->engine_fd = epoll_create1(EPOLL_CLOEXEC);

    if (engine->engine_fd < 0) {
        free(engine);
        return NULL;
    }

    return engine;
#else
    /* Not supported on non-Linux */
    return NULL;
#endif
}

/**
 * Create kqueue-based engine (macOS/BSD)
 */
static io_engine_t* io_engine_create_kqueue(void) {
#ifdef __APPLE__
    io_engine_t *engine = calloc(1, sizeof(io_engine_t));
    if (!engine) {
        return NULL;
    }

    engine->type = IO_ENGINE_KQUEUE;
    engine->engine_fd = kqueue();

    if (engine->engine_fd < 0) {
        free(engine);
        return NULL;
    }

    /* Set close-on-exec flag */
    fcntl(engine->engine_fd, F_SETFD, FD_CLOEXEC);

    return engine;
#else
    /* Not supported on non-macOS */
    return NULL;
#endif
}

#ifdef HAVE_IO_URING
/**
 * Create io_uring-based engine
 */
static io_engine_t* io_engine_create_uring(uint32_t queue_depth) {
    io_engine_t *engine = calloc(1, sizeof(io_engine_t));
    if (!engine) {
        return NULL;
    }

    engine->type = IO_ENGINE_URING;
    engine->queue_depth = queue_depth;

    /* Allocate io_uring structure */
    engine->ring = calloc(1, sizeof(struct io_uring));
    if (!engine->ring) {
        free(engine);
        return NULL;
    }

    /* Initialize io_uring with optimizations */
    struct io_uring_params params;
    memset(&params, 0, sizeof(params));

    int ret = io_uring_queue_init_params(queue_depth, engine->ring, &params);
    if (ret < 0) {
        free(engine->ring);
        free(engine);
        return NULL;
    }

    return engine;
}
#endif

/**
 * Create a new I/O engine
 */
io_engine_t* io_engine_create(uint32_t queue_depth) {
    if (queue_depth == 0) {
        queue_depth = DEFAULT_QUEUE_DEPTH;
    }

    /* Try io_uring first on Linux */
#ifdef HAVE_IO_URING
    if (io_engine_has_uring()) {
        io_engine_t *engine = io_engine_create_uring(queue_depth);
        if (engine) {
            printf("[io_engine] Using io_uring (queue_depth=%u)\n", queue_depth);
            return engine;
        }
    }
#endif

    /* Try epoll on Linux */
    io_engine_t *engine = io_engine_create_epoll();
    if (engine) {
        printf("[io_engine] Using epoll\n");
        return engine;
    }

    /* Try kqueue on macOS */
    engine = io_engine_create_kqueue();
    if (engine) {
        printf("[io_engine] Using kqueue\n");
        return engine;
    }

    /* Basic fallback - synchronous I/O */
    printf("[io_engine] Using synchronous I/O (no epoll/kqueue/io_uring available)\n");
    engine = calloc(1, sizeof(io_engine_t));
    if (engine) {
        engine->type = IO_ENGINE_EPOLL;  /* Placeholder */
        engine->engine_fd = -1;
    }
    return engine;
}

/**
 * Destroy an I/O engine
 */
void io_engine_destroy(io_engine_t *engine) {
    if (!engine) {
        return;
    }

#ifdef HAVE_IO_URING
    if (engine->type == IO_ENGINE_URING && engine->ring) {
        io_uring_queue_exit(engine->ring);
        free(engine->ring);
    }
#endif

    if (engine->engine_fd >= 0) {
        close(engine->engine_fd);
    }

    printf("[io_engine] Stats - submitted: %" PRIu64 ", completed: %" PRIu64 ", failed: %" PRIu64 "\n",
           engine->ops_submitted, engine->ops_completed, engine->ops_failed);

    free(engine);
}

/**
 * Submit an I/O operation
 */
int io_engine_submit(io_engine_t *engine, io_operation_t *op) {
    if (!engine || !op) {
        return -1;
    }

    /* For now, just a placeholder */
    return 0;
}

/**
 * Submit multiple I/O operations (batching)
 */
int io_engine_submit_batch(io_engine_t *engine, io_operation_t **ops, size_t count) {
    if (!engine || !ops || count == 0) {
        return -1;
    }

    int submitted = 0;
    for (size_t i = 0; i < count; i++) {
        if (io_engine_submit(engine, ops[i]) == 0) {
            submitted++;
        }
    }

    return submitted;
}

/**
 * Wait for I/O completions
 */
int io_engine_wait(io_engine_t *engine, uint32_t timeout_ms) {
    if (!engine) {
        return -1;
    }

    /* Placeholder */
    return 0;
}

/**
 * Process completed operations
 */
int io_engine_process_completions(io_engine_t *engine) {
    if (!engine) {
        return -1;
    }

    /* Placeholder */
    return 0;
}

/**
 * Create a non-blocking socket
 */
int io_socket_create_nonblocking(int domain, int type, int protocol) {
#ifdef __linux__
    int sockfd = socket(domain, type | SOCK_NONBLOCK | SOCK_CLOEXEC, protocol);
#else
    /* macOS and Windows don't support SOCK_NONBLOCK flag */
    int sockfd = (int)socket(domain, type, protocol);
    if (sockfd >= 0) {
#ifdef _WIN32
        u_long mode = 1;
        ioctlsocket(sockfd, FIONBIO, &mode);
#else
        int flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
        fcntl(sockfd, F_SETFD, FD_CLOEXEC);
#endif
    }
#endif
    return sockfd;
}

/**
 * Set socket options for performance
 */
int io_socket_set_performance_opts(int sockfd) {
    int opt = 1;

    /* Enable TCP_NODELAY (disable Nagle's algorithm) */
#ifdef _WIN32
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char*)&opt, sizeof(opt)) < 0) {
#else
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
#endif
        return -1;
    }

    /* Enable SO_KEEPALIVE */
#ifdef _WIN32
    if (setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, (char*)&opt, sizeof(opt)) < 0) {
#else
    if (setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt)) < 0) {
#endif
        return -1;
    }

    /* Set SO_RCVBUF and SO_SNDBUF for larger buffers */
    int buf_size = 256 * 1024;  /* 256KB */
#ifdef _WIN32
    setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (char*)&buf_size, sizeof(buf_size));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (char*)&buf_size, sizeof(buf_size));
#else
    setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
#endif

    return 0;
}

/* Operation helpers */

io_operation_t* io_op_create(io_op_type_t type) {
    io_operation_t *op = calloc(1, sizeof(io_operation_t));
    if (op) {
        op->type = type;
        op->fd = -1;
    }
    return op;
}

io_operation_t* io_op_connect_create(
    int sockfd,
    const struct sockaddr *addr,
    socklen_t addrlen,
    void (*callback)(io_operation_t *op),
    void *user_data)
{
    io_operation_t *op = io_op_create(IO_OP_CONNECT);
    if (op) {
        op->fd = sockfd;
        op->callback = callback;
        op->user_data = user_data;
    }
    return op;
}

io_operation_t* io_op_recv_create(
    int sockfd,
    void *buf,
    size_t len,
    void (*callback)(io_operation_t *op),
    void *user_data)
{
    io_operation_t *op = io_op_create(IO_OP_RECV);
    if (op) {
        op->fd = sockfd;
        op->buf = buf;
        op->len = len;
        op->callback = callback;
        op->user_data = user_data;
    }
    return op;
}

io_operation_t* io_op_send_create(
    int sockfd,
    const void *buf,
    size_t len,
    void (*callback)(io_operation_t *op),
    void *user_data)
{
    io_operation_t *op = io_op_create(IO_OP_SEND);
    if (op) {
        op->fd = sockfd;
        op->buf = (void*)buf;
        op->len = len;
        op->callback = callback;
        op->user_data = user_data;
    }
    return op;
}

void io_op_destroy(io_operation_t *op) {
    free(op);
}
