/**
 * IOCP Completion Dispatcher for Windows Async I/O
 *
 * Provides centralized completion handling for Windows IOCP operations.
 * Runs a dedicated thread that blocks on GetQueuedCompletionStatus and
 * dispatches completions to the appropriate async_request.
 */

#ifndef IOCP_DISPATCHER_H
#define IOCP_DISPATCHER_H

#ifdef _WIN32

#include <stdint.h>
#include <stdbool.h>

/* Forward declarations */
typedef struct io_engine io_engine_t;
typedef struct async_request async_request_t;

/**
 * Completion notification callback
 * Called when an IOCP operation completes
 *
 * @param request The request that owns the completed operation
 * @param bytes_transferred Number of bytes transferred
 * @param error Error code (0 for success)
 */
typedef void (*iocp_completion_callback_t)(async_request_t *request,
                                           uint32_t bytes_transferred,
                                           uint32_t error);

/**
 * Initialize and start the IOCP dispatcher thread
 *
 * @param engine The I/O engine with IOCP handle
 * @return 0 on success, -1 on failure
 */
int iocp_dispatcher_start(io_engine_t *engine);

/**
 * Stop the IOCP dispatcher thread and cleanup
 *
 * @param engine The I/O engine
 */
void iocp_dispatcher_stop(io_engine_t *engine);

/**
 * Register a completion callback for a request
 * Called when initiating an async operation
 *
 * @param request The request initiating the operation
 * @param callback Callback to invoke when operation completes
 */
void iocp_dispatcher_register_callback(async_request_t *request,
                                       iocp_completion_callback_t callback);

/**
 * Unregister a request's completion callback
 * Called when request is destroyed
 *
 * @param request The request to unregister
 */
void iocp_dispatcher_unregister_callback(async_request_t *request);

/**
 * Check if dispatcher is running
 *
 * @param engine The I/O engine
 * @return true if dispatcher thread is running
 */
bool iocp_dispatcher_is_running(io_engine_t *engine);

/**
 * Post a custom completion packet to IOCP
 * Used to wake up the dispatcher or send control messages
 *
 * @param engine The I/O engine
 * @param completion_key Custom completion key
 * @param bytes_transferred Bytes value for completion
 * @return 0 on success, -1 on failure
 */
int iocp_dispatcher_post_completion(io_engine_t *engine,
                                    uintptr_t completion_key,
                                    uint32_t bytes_transferred);

#endif /* _WIN32 */
#endif /* IOCP_DISPATCHER_H */
