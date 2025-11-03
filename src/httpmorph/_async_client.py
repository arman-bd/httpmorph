"""
AsyncClient - Truly asynchronous HTTP client using httpmorph's async I/O engine

This provides true async I/O capabilities without thread pool overhead.
Uses C-level I/O engine (kqueue/epoll) for non-blocking operations.
"""

import asyncio
from datetime import timedelta
from http.client import responses as http_responses

# Try to import the async bindings
try:
    from httpmorph import _async as _async_bindings

    HAS_ASYNC_BINDINGS = True
except ImportError:
    _async_bindings = None
    HAS_ASYNC_BINDINGS = False


class AsyncResponse:
    """Response object for async requests (similar to sync Response)"""

    def __init__(self, response_dict: dict, url: str):
        self.status_code = response_dict["status_code"]
        self.headers = response_dict["headers"]
        self.body = response_dict["body"]
        self.url = url

        # Store raw http_version enum for lazy formatting
        self._http_version_enum = response_dict["http_version"]
        self._http_version = None

        # Timing information (in microseconds)
        self.connect_time_us = response_dict["connect_time_us"]
        self.tls_time_us = response_dict["tls_time_us"]
        self.first_byte_time_us = response_dict["first_byte_time_us"]
        self.total_time_us = response_dict["total_time_us"]

        # TLS information
        self.tls_version = response_dict["tls_version"]
        self.tls_cipher = response_dict["tls_cipher"]
        self.ja3_fingerprint = response_dict["ja3_fingerprint"]

        # Lazy text decoding
        self._text = None
        self._json = None

        # Error information
        self.error = response_dict["error"]
        self.error_message = response_dict["error_message"]

    def _format_http_version(self, version_enum):
        """Convert HTTP version enum to string"""
        version_map = {
            0: "1.0",
            1: "1.1",
            2: "2.0",
            3: "3.0",
        }
        return version_map.get(version_enum, "1.1")

    @property
    def http_version(self):
        """Get HTTP version string (lazy evaluation)"""
        if self._http_version is None:
            self._http_version = self._format_http_version(self._http_version_enum)
        return self._http_version

    @property
    def content(self):
        """Alias for body (requests compatibility)"""
        return self.body

    @property
    def text(self):
        """Decode body as text (lazy evaluation)"""
        if self._text is None:
            try:
                self._text = self.body.decode("utf-8")
            except (UnicodeDecodeError, AttributeError):
                self._text = self.body.decode("latin-1", errors="replace") if self.body else ""
        return self._text

    def json(self, **kwargs):
        """Decode body as JSON (lazy evaluation)"""
        if self._json is None:
            import json

            if not self.body:
                raise ValueError("No JSON content in response")
            try:
                self._json = json.loads(self.text)
            except json.JSONDecodeError as e:
                raise ValueError(f"Invalid JSON: {e}") from e
        return self._json

    @property
    def ok(self):
        """True if status code is less than 400"""
        return 200 <= self.status_code < 400

    @property
    def is_redirect(self):
        """True if status code is a redirect (3xx)"""
        return self.status_code in (301, 302, 303, 307, 308)

    @property
    def reason(self):
        """HTTP status reason phrase"""
        return http_responses.get(self.status_code, "Unknown")

    @property
    def elapsed(self):
        """Time elapsed for the request as a timedelta"""
        seconds = self.total_time_us / 1_000_000.0
        return timedelta(seconds=seconds)

    def raise_for_status(self):
        """Raise HTTPError if status code indicates an error"""
        if 400 <= self.status_code < 600:
            from httpmorph._client_c import HTTPError

            raise HTTPError(f"{self.status_code} Error: {self.reason}", response=self)
        return self


class AsyncClient:
    """
    HTTP client with true async I/O (no thread pool)

    This uses the C-level async I/O engine for maximum performance:
    - ✅ Non-blocking connect()
    - ✅ Non-blocking TLS handshake
    - ✅ Non-blocking send/receive
    - ✅ I/O engine with epoll/kqueue
    - ✅ Async request manager
    - ✅ Python asyncio bindings
    - ⏳ DNS resolution (uses blocking for now)

    Usage:
        async with AsyncClient() as client:
            response = await client.get('https://example.com')
            print(response.status_code)
    """

    def __init__(self, http2: bool = False, timeout: float = 30.0):
        """
        Initialize AsyncClient

        Args:
            http2: Enable HTTP/2 support (not yet implemented)
            timeout: Default timeout in seconds
        """
        if not HAS_ASYNC_BINDINGS:
            raise RuntimeError(
                "Async bindings not available. "
                "Please rebuild httpmorph with: python setup.py build_ext --inplace"
            )

        self.http2 = http2
        self.timeout = timeout
        self._manager = None
        self._loop = None

    async def __aenter__(self):
        """Async context manager entry"""
        # Create manager and set event loop
        self._manager = _async_bindings.create_async_manager()
        self._loop = asyncio.get_running_loop()
        self._manager.set_event_loop(self._loop)
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        """Async context manager exit"""
        await self.close()

    async def get(self, url: str, **kwargs):
        """
        Make async GET request

        Args:
            url: URL to request
            **kwargs: Additional request options (headers, timeout)

        Returns:
            AsyncResponse object
        """
        return await self._request("GET", url, **kwargs)

    async def post(self, url: str, **kwargs):
        """Make async POST request"""
        return await self._request("POST", url, **kwargs)

    async def put(self, url: str, **kwargs):
        """Make async PUT request"""
        return await self._request("PUT", url, **kwargs)

    async def delete(self, url: str, **kwargs):
        """Make async DELETE request"""
        return await self._request("DELETE", url, **kwargs)

    async def head(self, url: str, **kwargs):
        """Make async HEAD request"""
        return await self._request("HEAD", url, **kwargs)

    async def patch(self, url: str, **kwargs):
        """Make async PATCH request"""
        return await self._request("PATCH", url, **kwargs)

    async def options(self, url: str, **kwargs):
        """Make async OPTIONS request"""
        return await self._request("OPTIONS", url, **kwargs)

    async def _request(self, method: str, url: str, **kwargs):
        """
        Internal async request implementation

        This uses the C-level async I/O engine:
        1. Create C async_request_t via manager
        2. Get socket FD from async_request_get_fd()
        3. Register FD with asyncio event loop (add_reader/add_writer)
        4. Wait for I/O events without blocking
        5. Step state machine on each event
        6. Return response when complete
        """
        if self._manager is None:
            raise RuntimeError(
                "Client not initialized. Use 'async with AsyncClient() as client:' pattern"
            )

        # Get timeout (use default if not specified)
        timeout = kwargs.get("timeout", self.timeout)
        timeout_ms = int(timeout * 1000)

        # Get headers
        headers = kwargs.get("headers", {})

        # Get verify parameter (default to True)
        verify = kwargs.get("verify", True)

        # Get proxy parameters
        proxy = kwargs.get("proxy") or kwargs.get("proxies")
        proxy_auth = kwargs.get("proxy_auth")

        # Get body
        body = kwargs.get("data") or kwargs.get("body")

        # Handle JSON parameter
        json_data = kwargs.get("json")
        if json_data:
            import json

            body = json.dumps(json_data).encode("utf-8")
            headers = headers.copy()  # Don't modify original
            headers["Content-Type"] = "application/json"

        # Convert body to bytes if needed
        if body and isinstance(body, str):
            body = body.encode("utf-8")

        # Submit request to manager
        response_dict = await self._manager.submit_request(
            method=method,
            url=url,
            headers=headers,
            body=body,
            timeout_ms=timeout_ms,
            verify=verify,
            proxy=proxy,
            proxy_auth=proxy_auth,
        )

        # Check for errors
        if response_dict.get("error") and response_dict["error"] != 0:
            error_msg = response_dict.get("error_message", "Request failed")
            error_code = response_dict["error"]

            # Map error codes to exceptions (negative values in C)
            if error_code == -5:  # HTTPMORPH_ERROR_TIMEOUT
                raise asyncio.TimeoutError(error_msg)
            elif error_code == -3:  # HTTPMORPH_ERROR_NETWORK
                from httpmorph._client_c import ConnectionError

                raise ConnectionError(error_msg)
            else:
                from httpmorph._client_c import RequestException

                raise RequestException(error_msg)

        # Create response object
        return AsyncResponse(response_dict, url)

    async def close(self):
        """Close client and cleanup resources"""
        if self._manager is not None:
            # Wait for all active requests to complete before destroying manager
            # This prevents the manager from being destroyed mid-request
            max_wait = 10  # seconds
            wait_start = asyncio.get_running_loop().time()
            while self._manager.get_active_count() > 0:
                # Trigger cleanup of completed requests
                self._manager.cleanup()

                # Check if any requests remain
                active = self._manager.get_active_count()
                if active == 0:
                    break

                elapsed = asyncio.get_running_loop().time() - wait_start
                if elapsed > max_wait:
                    print(
                        f"[AsyncClient] Warning: {active} requests still active after {max_wait}s timeout"
                    )
                    break
                await asyncio.sleep(0.1)  # Give poll loops time to complete

            # Give any remaining poll loops a chance to complete
            # This ensures all async coroutines have exited before manager destruction
            await asyncio.sleep(0.2)

            # Manager cleanup is handled by Cython __dealloc__
            self._manager = None
        self._loop = None


# Architecture documentation
__doc__ = """
Async I/O Architecture (Phase B Complete)
==========================================

Phase B Days 1-3: ✅ COMPLETE

1. I/O Engine (src/core/io_engine.c)
   - epoll support for Linux (edge-triggered)
   - kqueue support for macOS/BSD (one-shot)
   - Platform-agnostic API
   - Socket helpers (non-blocking, performance opts)
   - Operation helpers (connect, recv, send)

2. Async Request State Machine (src/core/async_request.c)
   - 9-state machine: INIT → DNS → CONNECT → TLS → SEND → RECV_HEADERS → RECV_BODY → COMPLETE
   - Non-blocking at every stage
   - Proper SSL_WANT_READ/WANT_WRITE handling
   - Timeout tracking
   - Error handling
   - Reference counting

3. Request Manager (src/core/async_request_manager.c)
   - Track multiple concurrent requests
   - Request ID generation
   - Event loop integration
   - Thread-safe operations

Phase B Days 4-5: ⏳ IN PROGRESS

4. Python Asyncio Integration (this file)
   - Cython bindings for async APIs
   - Event loop integration (add_reader/add_writer)
   - AsyncClient class (this file)
   - Example applications

Architecture Benefits:
- No thread pool overhead (currently 1-2ms per request)
- Support for 10,000+ concurrent connections
- Sub-millisecond async overhead
- Efficient resource usage (320KB per request vs 8MB per thread)
- Native event loop integration

Performance Targets:
- Latency: 100-200μs overhead (vs 1-2ms with thread pool)
- Concurrency: 10K+ simultaneous requests (vs 100-200 with threads)
- Memory: 320KB per request (vs 8MB per thread)
- Throughput: 2-5x improvement over thread pool approach
"""
