/**
 * IOCP Completion Dispatcher Implementation
 */

#ifdef _WIN32

#include "iocp_dispatcher.h"
#include "io_engine.h"
#include "async_request.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

/* Debug output control */
#ifdef HTTPMORPH_DEBUG
    #define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
    #define DEBUG_PRINT(...) ((void)0)
#endif

/* Special completion key to signal dispatcher shutdown */
#define IOCP_SHUTDOWN_KEY ((ULONG_PTR)-1)

/**
 * Dispatcher state
 */
typedef struct {
    io_engine_t *engine;
    HANDLE thread;
    DWORD thread_id;
    volatile bool running;
    volatile bool shutdown_requested;
} iocp_dispatcher_state_t;

/* Global dispatcher state (one per engine) */
static iocp_dispatcher_state_t *dispatcher_state = NULL;

/**
 * Dispatcher thread main function
 */
static DWORD WINAPI iocp_dispatcher_thread(LPVOID param) {
    iocp_dispatcher_state_t *state = (iocp_dispatcher_state_t*)param;
    io_engine_t *engine = state->engine;

    DEBUG_PRINT("[iocp_dispatcher] Thread started (tid=%lu)\n", GetCurrentThreadId());

    while (!state->shutdown_requested) {
        DWORD bytes_transferred = 0;
        ULONG_PTR completion_key = 0;
        OVERLAPPED *overlapped = NULL;

        /* Block waiting for completion (INFINITE timeout) */
        BOOL result = GetQueuedCompletionStatus(
            (HANDLE)engine->iocp_handle,
            &bytes_transferred,
            &completion_key,
            &overlapped,
            INFINITE  /* Block until completion */
        );

        /* Check for shutdown signal */
        if (completion_key == IOCP_SHUTDOWN_KEY) {
            DEBUG_PRINT("[iocp_dispatcher] Shutdown signal received\n");
            break;
        }

        /* Get error if operation failed */
        DWORD error = 0;
        if (!result && overlapped != NULL) {
            error = GetLastError();
        } else if (!result) {
            /* GetQueuedCompletionStatus itself failed */
            error = GetLastError();
            DEBUG_PRINT("[iocp_dispatcher] GetQueuedCompletionStatus failed: %lu\n", error);
            continue;
        }

        /* completion_key is the async_request_t pointer */
        async_request_t *request = (async_request_t*)completion_key;

        if (request == NULL) {
            DEBUG_PRINT("[iocp_dispatcher] WARNING: NULL request for completion\n");
            continue;
        }

        DEBUG_PRINT("[iocp_dispatcher] Completion: request=%p, bytes=%lu, error=%lu, overlapped=%p\n",
               request, bytes_transferred, error, overlapped);

        /* Store completion info in request */
        request->iocp_last_error = error;
        request->iocp_bytes_transferred = bytes_transferred;
        request->iocp_operation_pending = false;

        /* Signal the request that operation completed */
        if (request->iocp_completion_event) {
            SetEvent((HANDLE)request->iocp_completion_event);
        }

        /* Call completion callback if registered */
        if (request->iocp_completion_callback) {
            request->iocp_completion_callback(request, bytes_transferred, error);
        }
    }

    DEBUG_PRINT("[iocp_dispatcher] Thread exiting\n");
    state->running = false;
    return 0;
}

/**
 * Start the IOCP dispatcher thread
 */
int iocp_dispatcher_start(io_engine_t *engine) {
    if (engine == NULL || engine->iocp_handle == NULL) {
        return -1;
    }

    /* Check if already running */
    if (dispatcher_state != NULL && dispatcher_state->running) {
        DEBUG_PRINT("[iocp_dispatcher] Already running\n");
        return 0;
    }

    /* Allocate state */
    if (dispatcher_state == NULL) {
        dispatcher_state = (iocp_dispatcher_state_t*)calloc(1, sizeof(iocp_dispatcher_state_t));
        if (dispatcher_state == NULL) {
            return -1;
        }
    }

    dispatcher_state->engine = engine;
    dispatcher_state->running = false;
    dispatcher_state->shutdown_requested = false;

    /* Create dispatcher thread */
    dispatcher_state->thread = CreateThread(
        NULL,                          /* Security attributes */
        0,                             /* Stack size (default) */
        iocp_dispatcher_thread,        /* Thread function */
        dispatcher_state,              /* Parameter */
        0,                             /* Creation flags */
        &dispatcher_state->thread_id   /* Thread ID */
    );

    if (dispatcher_state->thread == NULL) {
        DEBUG_PRINT("[iocp_dispatcher] Failed to create thread: %lu\n", GetLastError());
        free(dispatcher_state);
        dispatcher_state = NULL;
        return -1;
    }

    dispatcher_state->running = true;
    DEBUG_PRINT("[iocp_dispatcher] Started successfully (tid=%lu)\n", dispatcher_state->thread_id);

    return 0;
}

/**
 * Stop the IOCP dispatcher thread
 */
void iocp_dispatcher_stop(io_engine_t *engine) {
    if (dispatcher_state == NULL || !dispatcher_state->running) {
        return;
    }

    DEBUG_PRINT("[iocp_dispatcher] Stopping...\n");

    /* Request shutdown */
    dispatcher_state->shutdown_requested = true;

    /* Post shutdown completion to wake up thread */
    PostQueuedCompletionStatus(
        (HANDLE)engine->iocp_handle,
        0,
        IOCP_SHUTDOWN_KEY,
        NULL
    );

    /* Wait for thread to exit (max 5 seconds) */
    DWORD wait_result = WaitForSingleObject(dispatcher_state->thread, 5000);

    if (wait_result == WAIT_TIMEOUT) {
        DEBUG_PRINT("[iocp_dispatcher] WARNING: Thread did not exit cleanly, terminating\n");
        TerminateThread(dispatcher_state->thread, 1);
    }

    /* Cleanup */
    CloseHandle(dispatcher_state->thread);
    free(dispatcher_state);
    dispatcher_state = NULL;

    DEBUG_PRINT("[iocp_dispatcher] Stopped\n");
}

/**
 * Register completion callback for a request
 */
void iocp_dispatcher_register_callback(async_request_t *request,
                                       iocp_completion_callback_t callback) {
    if (request) {
        request->iocp_completion_callback = callback;
    }
}

/**
 * Unregister a request's completion callback
 */
void iocp_dispatcher_unregister_callback(async_request_t *request) {
    if (request) {
        request->iocp_completion_callback = NULL;
    }
}

/**
 * Check if dispatcher is running
 */
bool iocp_dispatcher_is_running(io_engine_t *engine) {
    (void)engine; /* Unused */
    return dispatcher_state != NULL && dispatcher_state->running;
}

/**
 * Post a custom completion packet
 */
int iocp_dispatcher_post_completion(io_engine_t *engine,
                                    uintptr_t completion_key,
                                    uint32_t bytes_transferred) {
    if (engine == NULL || engine->iocp_handle == NULL) {
        return -1;
    }

    BOOL result = PostQueuedCompletionStatus(
        (HANDLE)engine->iocp_handle,
        bytes_transferred,
        completion_key,
        NULL
    );

    return result ? 0 : -1;
}

#endif /* _WIN32 */
