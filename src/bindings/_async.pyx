# cython: language_level=3
# distutils: language=c
"""
_async.pyx - Cython bindings for httpmorph async I/O APIs

Provides Python asyncio integration for the C-level async request engine.
"""

from libc.stdint cimport uint8_t, uint16_t, uint32_t, uint64_t
from libc.stdlib cimport malloc, free
from libc.string cimport strdup
from libc.stdio cimport printf

import asyncio
from typing import Optional, Dict, Any


# External C declarations
cdef extern from "../core/async_request.h":
    # Async request states
    ctypedef enum async_request_state_t:
        ASYNC_STATE_INIT
        ASYNC_STATE_DNS_LOOKUP
        ASYNC_STATE_CONNECTING
        ASYNC_STATE_TLS_HANDSHAKE
        ASYNC_STATE_SENDING_REQUEST
        ASYNC_STATE_RECEIVING_HEADERS
        ASYNC_STATE_RECEIVING_BODY
        ASYNC_STATE_COMPLETE
        ASYNC_STATE_ERROR

    # Async request status
    ctypedef enum async_request_status_t:
        ASYNC_STATUS_IN_PROGRESS
        ASYNC_STATUS_COMPLETE
        ASYNC_STATUS_ERROR
        ASYNC_STATUS_NEED_READ
        ASYNC_STATUS_NEED_WRITE

    # Forward declarations
    ctypedef struct async_request_t
    ctypedef struct httpmorph_request_t
    ctypedef struct httpmorph_response_t
    ctypedef struct io_engine_t

    # Callback type
    ctypedef void (*async_request_callback_t)(async_request_t *req, int status)

    # Async request functions
    async_request_t* async_request_create(
        const httpmorph_request_t *request,
        io_engine_t *io_engine,
        uint32_t timeout_ms,
        async_request_callback_t callback,
        void *user_data
    ) nogil

    void async_request_destroy(async_request_t *req) nogil
    void async_request_ref(async_request_t *req) nogil
    void async_request_unref(async_request_t *req) nogil
    int async_request_step(async_request_t *req) nogil
    async_request_state_t async_request_get_state(const async_request_t *req) nogil
    const char* async_request_state_name(async_request_state_t state) nogil
    int async_request_get_fd(const async_request_t *req) nogil
    bint async_request_is_timeout(const async_request_t *req) nogil
    void async_request_set_error(async_request_t *req, int error_code, const char *error_msg) nogil
    httpmorph_response_t* async_request_get_response(async_request_t *req) nogil
    const char* async_request_get_error_message(const async_request_t *req) nogil


cdef extern from "../core/async_request_manager.h":
    # Request manager structure
    ctypedef struct async_request_manager_t

    # Manager functions
    async_request_manager_t* async_manager_create() nogil
    void async_manager_destroy(async_request_manager_t *mgr) nogil
    uint64_t async_manager_submit_request(
        async_request_manager_t *mgr,
        const httpmorph_request_t *request,
        uint32_t timeout_ms,
        async_request_callback_t callback,
        void *user_data
    ) nogil
    async_request_t* async_manager_get_request(
        async_request_manager_t *mgr,
        uint64_t request_id
    ) nogil
    int async_manager_cancel_request(
        async_request_manager_t *mgr,
        uint64_t request_id
    ) nogil
    int async_manager_poll(
        async_request_manager_t *mgr,
        uint32_t timeout_ms
    ) nogil
    int async_manager_process(async_request_manager_t *mgr) nogil
    size_t async_manager_get_active_count(const async_request_manager_t *mgr) nogil
    int async_manager_start_event_loop(async_request_manager_t *mgr) nogil
    int async_manager_stop_event_loop(async_request_manager_t *mgr) nogil


cdef extern from "../core/io_engine.h":
    ctypedef struct io_engine_t

    io_engine_t* io_engine_create(uint32_t queue_depth) nogil
    void io_engine_destroy(io_engine_t *engine) nogil


cdef extern from "../include/httpmorph.h":
    # HTTP methods
    ctypedef enum httpmorph_method_t:
        HTTPMORPH_GET
        HTTPMORPH_POST
        HTTPMORPH_PUT
        HTTPMORPH_DELETE
        HTTPMORPH_HEAD
        HTTPMORPH_OPTIONS
        HTTPMORPH_PATCH

    # HTTP version
    ctypedef enum httpmorph_version_t:
        HTTPMORPH_VERSION_1_0
        HTTPMORPH_VERSION_1_1
        HTTPMORPH_VERSION_2_0
        HTTPMORPH_VERSION_3_0

    # Error codes
    ctypedef enum httpmorph_error_t:
        HTTPMORPH_OK
        HTTPMORPH_ERROR_MEMORY
        HTTPMORPH_ERROR_INVALID_PARAM
        HTTPMORPH_ERROR_NETWORK
        HTTPMORPH_ERROR_TLS
        HTTPMORPH_ERROR_TIMEOUT
        HTTPMORPH_ERROR_PARSE
        HTTPMORPH_ERROR_PROTOCOL

    # Header structure
    ctypedef struct httpmorph_header_t:
        char *key
        char *value

    # Response structure
    ctypedef struct httpmorph_response_t:
        uint16_t status_code
        httpmorph_version_t http_version
        httpmorph_header_t *headers
        size_t header_count
        uint8_t *body
        size_t body_len
        uint64_t connect_time_us
        uint64_t tls_time_us
        uint64_t first_byte_time_us
        uint64_t total_time_us
        char *tls_version
        char *tls_cipher
        char *ja3_fingerprint
        httpmorph_error_t error
        char *error_message

    # Request functions
    httpmorph_request_t* httpmorph_request_create(httpmorph_method_t method, const char *url) nogil
    void httpmorph_request_destroy(httpmorph_request_t *request) nogil
    int httpmorph_request_add_header(httpmorph_request_t *request, const char *key, const char *value) nogil
    int httpmorph_request_set_body(httpmorph_request_t *request, const uint8_t *body, size_t body_len) nogil
    void httpmorph_request_set_timeout(httpmorph_request_t *request, uint32_t timeout_ms) nogil

    # Response functions
    void httpmorph_response_destroy(httpmorph_response_t *response) nogil


# Python wrapper classes

cdef class AsyncRequestManager:
    """Manager for multiple concurrent async HTTP requests"""
    cdef async_request_manager_t *_manager
    cdef object _loop  # asyncio event loop
    cdef dict _pending_requests  # request_id -> Future mapping

    def __cinit__(self):
        with nogil:
            self._manager = async_manager_create()
        if self._manager is NULL:
            raise MemoryError("Failed to create async request manager")
        self._pending_requests = {}
        self._loop = None

    def __dealloc__(self):
        if self._manager is not NULL:
            with nogil:
                async_manager_destroy(self._manager)
            self._manager = NULL

    def set_event_loop(self, loop):
        """Set the asyncio event loop to use"""
        self._loop = loop

    async def submit_request(
        self,
        str method,
        str url,
        dict headers=None,
        bytes body=None,
        uint32_t timeout_ms=30000
    ):
        """Submit an async HTTP request and return a Future

        Args:
            method: HTTP method (GET, POST, etc.)
            url: URL to request
            headers: Optional dict of headers
            body: Optional request body
            timeout_ms: Timeout in milliseconds

        Returns:
            dict: Response dictionary with status_code, headers, body, etc.
        """
        # Declare all cdef variables at the top
        cdef httpmorph_method_t c_method
        cdef httpmorph_request_t *req
        cdef uint64_t request_id

        # Convert method to enum
        method_upper = method.upper()
        if method_upper == "GET":
            c_method = HTTPMORPH_GET
        elif method_upper == "POST":
            c_method = HTTPMORPH_POST
        elif method_upper == "PUT":
            c_method = HTTPMORPH_PUT
        elif method_upper == "DELETE":
            c_method = HTTPMORPH_DELETE
        elif method_upper == "HEAD":
            c_method = HTTPMORPH_HEAD
        elif method_upper == "OPTIONS":
            c_method = HTTPMORPH_OPTIONS
        elif method_upper == "PATCH":
            c_method = HTTPMORPH_PATCH
        else:
            c_method = HTTPMORPH_GET

        # Create request
        url_bytes = url.encode('utf-8')
        req = httpmorph_request_create(c_method, <const char*>url_bytes)

        if req is NULL:
            raise MemoryError("Failed to create request")

        try:
            # Set timeout
            httpmorph_request_set_timeout(req, timeout_ms)

            # Add headers
            if headers:
                for key, value in headers.items():
                    key_bytes = key.encode('utf-8')
                    value_bytes = value.encode('utf-8')
                    httpmorph_request_add_header(req, <const char*>key_bytes, <const char*>value_bytes)

            # Set body
            if body:
                httpmorph_request_set_body(req, <const uint8_t*>body, len(body))

            # Create a Future for this request
            future = asyncio.Future()

            # Submit to manager
            # Note: We can't use callbacks from C to Python easily,
            # so we'll poll the request instead
            request_id = async_manager_submit_request(
                self._manager,
                req,
                timeout_ms,
                NULL,  # No callback for now
                NULL   # No user data
            )

            if request_id == 0:
                raise RuntimeError("Failed to submit request")

            # Store the future
            self._pending_requests[request_id] = future

            # Start polling for this request
            await self._poll_request(request_id, future)

            # Wait for completion
            return await future

        finally:
            httpmorph_request_destroy(req)

    async def _poll_request(self, uint64_t request_id, future):
        """Poll a request until it completes"""
        # Declare all cdef variables at the top
        cdef async_request_t *req = NULL
        cdef int status
        cdef int fd
        cdef async_request_state_t state
        cdef bint is_timeout

        while not future.done():
            # Get request (adds a reference)
            req = async_manager_get_request(self._manager, request_id)

            if req is NULL:
                future.set_exception(RuntimeError("Request not found"))
                return

            try:
                # Check for timeout
                is_timeout = async_request_is_timeout(req)

                if is_timeout:
                    async_request_unref(req)
                    future.set_exception(TimeoutError("Request timed out"))
                    return

                # Step the state machine (always, even without FD for early states)
                status = async_request_step(req)

                if status == ASYNC_STATUS_COMPLETE:
                    # Request completed successfully
                    response = self._extract_response(req)
                    async_request_unref(req)
                    future.set_result(response)
                    return

                elif status == ASYNC_STATUS_ERROR:
                    # Request failed
                    state = async_request_get_state(req)
                    state_name = async_request_state_name(state).decode('utf-8') if state else "UNKNOWN"

                    # Get error message from request
                    error_msg_ptr = async_request_get_error_message(req)
                    error_msg = error_msg_ptr.decode('utf-8') if error_msg_ptr is not NULL else "Unknown error"

                    async_request_unref(req)
                    future.set_exception(RuntimeError(f"Request failed in state {state_name}: {error_msg}"))
                    return

                elif status == ASYNC_STATUS_NEED_READ or status == ASYNC_STATUS_NEED_WRITE:
                    # Get file descriptor for event loop integration
                    fd = async_request_get_fd(req)

                    if fd >= 0 and self._loop:
                        if status == ASYNC_STATUS_NEED_READ:
                            # Wait for socket to be readable
                            read_event = asyncio.Event()
                            self._loop.add_reader(fd, read_event.set)
                            try:
                                await asyncio.wait_for(read_event.wait(), timeout=0.1)
                            except asyncio.TimeoutError:
                                pass  # Continue polling
                            finally:
                                self._loop.remove_reader(fd)
                        else:  # ASYNC_STATUS_NEED_WRITE
                            # Wait for socket to be writable
                            write_event = asyncio.Event()
                            self._loop.add_writer(fd, write_event.set)
                            try:
                                await asyncio.wait_for(write_event.wait(), timeout=0.1)
                            except asyncio.TimeoutError:
                                pass  # Continue polling
                            finally:
                                self._loop.remove_writer(fd)
                    else:
                        # FD not ready yet, short sleep
                        await asyncio.sleep(0.001)

                else:
                    # In progress, continue with short delay
                    await asyncio.sleep(0.001)

            finally:
                # Always unref the request we got at the start of the loop
                async_request_unref(req)

    cdef dict _extract_response(self, async_request_t *req):
        """Extract response from completed request"""
        cdef httpmorph_response_t *resp

        resp = async_request_get_response(req)

        if resp is NULL:
            return {
                'status_code': 0,
                'headers': {},
                'body': b'',
                'error': HTTPMORPH_ERROR_NETWORK,
                'error_message': 'No response'
            }

        # Build response dict
        result = {
            'status_code': resp.status_code,
            'headers': {},
            'body': bytes(resp.body[:resp.body_len]) if resp.body else b'',
            'http_version': resp.http_version,
            'connect_time_us': resp.connect_time_us,
            'tls_time_us': resp.tls_time_us,
            'first_byte_time_us': resp.first_byte_time_us,
            'total_time_us': resp.total_time_us,
            'tls_version': resp.tls_version.decode('utf-8') if resp.tls_version else None,
            'tls_cipher': resp.tls_cipher.decode('utf-8') if resp.tls_cipher else None,
            'ja3_fingerprint': resp.ja3_fingerprint.decode('utf-8') if resp.ja3_fingerprint else None,
            'error': resp.error,
            'error_message': resp.error_message.decode('utf-8') if resp.error_message else None,
        }

        # Convert headers
        for i in range(resp.header_count):
            key = resp.headers[i].key.decode('latin-1')
            try:
                value = resp.headers[i].value.decode('latin-1')
            except:
                value = resp.headers[i].value.decode('utf-8', errors='replace')
            result['headers'][key] = value

        return result

    def get_active_count(self):
        """Get number of active requests"""
        cdef size_t count
        with nogil:
            count = async_manager_get_active_count(self._manager)
        return count

    def cleanup(self):
        """Trigger cleanup of completed requests"""
        cdef int result
        with nogil:
            # Poll with 0 timeout just triggers cleanup without waiting
            result = async_manager_poll(self._manager, 0)
        return result


# Expose the manager to Python
def create_async_manager():
    """Create a new async request manager"""
    return AsyncRequestManager()
