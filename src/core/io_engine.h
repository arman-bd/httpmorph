/**
 * io_engine.h - High-performance I/O engine with io_uring and epoll support
 *
 * Provides a unified interface for:
 * - io_uring on Linux 5.1+ (highest performance)
 * - epoll on Linux (fallback)
 * - kqueue on macOS/BSD (future)
 */

#ifndef IO_ENGINE_H
#define IO_ENGINE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Platform-specific socket headers */
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef int socklen_t;
#else
    #include <sys/socket.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* I/O engine types */
typedef enum {
    IO_ENGINE_URING,    /* io_uring (Linux 5.1+) */
    IO_ENGINE_EPOLL,    /* epoll (Linux) */
    IO_ENGINE_KQUEUE,   /* kqueue (macOS/BSD) */
} io_engine_type_t;

/* I/O operation types */
typedef enum {
    IO_OP_ACCEPT,
    IO_OP_CONNECT,
    IO_OP_RECV,
    IO_OP_SEND,
    IO_OP_CLOSE,
    IO_OP_TIMEOUT,
} io_op_type_t;

/* I/O operation structure */
typedef struct io_operation {
    io_op_type_t type;
    int fd;

    /* Buffer for read/write */
    void *buf;
    size_t len;

    /* Result */
    int result;

    /* User data */
    void *user_data;

    /* Callback when operation completes */
    void (*callback)(struct io_operation *op);
} io_operation_t;

/* I/O engine structure */
typedef struct io_engine {
    io_engine_type_t type;
    int engine_fd;  /* epoll fd or io_uring fd */

    /* io_uring specific */
#ifdef HAVE_IO_URING
    struct io_uring *ring;
#endif

    /* Statistics */
    uint64_t ops_submitted;
    uint64_t ops_completed;
    uint64_t ops_failed;

    /* Configuration */
    uint32_t queue_depth;  /* io_uring queue depth */
    bool zero_copy;        /* Enable zero-copy operations */
} io_engine_t;

/* API */

/**
 * Create a new I/O engine
 * Automatically selects the best available engine for the platform
 */
io_engine_t* io_engine_create(uint32_t queue_depth);

/**
 * Destroy an I/O engine
 */
void io_engine_destroy(io_engine_t *engine);

/**
 * Submit an I/O operation
 * Returns 0 on success, -1 on error
 */
int io_engine_submit(io_engine_t *engine, io_operation_t *op);

/**
 * Submit multiple I/O operations (batching for performance)
 * Returns number of operations submitted, -1 on error
 */
int io_engine_submit_batch(
    io_engine_t *engine,
    io_operation_t **ops,
    size_t count
);

/**
 * Wait for I/O completions
 * Returns number of operations completed, -1 on error
 *
 * @param timeout_ms: Timeout in milliseconds, 0 for non-blocking, -1 for blocking
 */
int io_engine_wait(
    io_engine_t *engine,
    uint32_t timeout_ms
);

/**
 * Process completed operations
 * Calls the callbacks for completed operations
 */
int io_engine_process_completions(io_engine_t *engine);

/**
 * Get engine type name
 */
const char* io_engine_type_name(io_engine_type_t type);

/* Operation helpers */

/**
 * Create a connect operation
 */
io_operation_t* io_op_connect_create(
    int sockfd,
    const struct sockaddr *addr,
    socklen_t addrlen,
    void (*callback)(io_operation_t *op),
    void *user_data
);

/**
 * Create a receive operation
 */
io_operation_t* io_op_recv_create(
    int sockfd,
    void *buf,
    size_t len,
    void (*callback)(io_operation_t *op),
    void *user_data
);

/**
 * Create a send operation
 */
io_operation_t* io_op_send_create(
    int sockfd,
    const void *buf,
    size_t len,
    void (*callback)(io_operation_t *op),
    void *user_data
);

/**
 * Destroy an operation
 */
void io_op_destroy(io_operation_t *op);

/**
 * Check if io_uring is available on this system
 */
bool io_engine_has_uring(void);

/**
 * Create a non-blocking socket
 */
int io_socket_create_nonblocking(int domain, int type, int protocol);

/**
 * Set socket options for performance
 */
int io_socket_set_performance_opts(int sockfd);

#ifdef __cplusplus
}
#endif

#endif /* IO_ENGINE_H */
