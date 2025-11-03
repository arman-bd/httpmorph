# cython: language_level=3
# distutils: language=c
"""
_httpmorph.pyx - Cython bindings for httpmorph C library

Provides Python interface to the high-performance C HTTP client
with dynamic browser fingerprinting.
"""

from libc.stdint cimport uint8_t, uint16_t, uint32_t, uint64_t
from libc.stdlib cimport malloc, free
from libc.string cimport strdup
from libc.stdio cimport printf

# External C declarations
cdef extern from "../include/httpmorph.h":
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

    # Browser types
    ctypedef enum httpmorph_browser_t:
        HTTPMORPH_BROWSER_CHROME
        HTTPMORPH_BROWSER_FIREFOX
        HTTPMORPH_BROWSER_SAFARI
        HTTPMORPH_BROWSER_EDGE
        HTTPMORPH_BROWSER_RANDOM

    # Forward declarations
    ctypedef struct httpmorph_client_t
    ctypedef struct httpmorph_session_t
    ctypedef struct httpmorph_pool_t

    # Forward declarations
    ctypedef struct httpmorph_request_t
    ctypedef httpmorph_request_t httpmorph_request
    ctypedef struct httpmorph_response_t

    # Header structure for better cache locality
    ctypedef struct httpmorph_header_t:
        char *key
        char *value

    # Response structure (match the header exactly)
    struct httpmorph_response:
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

    # Core API
    int httpmorph_init()
    void httpmorph_cleanup()
    const char* httpmorph_version()

    # Client API
    httpmorph_client_t* httpmorph_client_create()
    int httpmorph_client_load_ca_file(httpmorph_client_t *client, const char *ca_file)
    void httpmorph_client_destroy(httpmorph_client_t *client)

    # Request API
    httpmorph_request_t* httpmorph_request_create(httpmorph_method_t method, const char *url) nogil
    void httpmorph_request_destroy(httpmorph_request_t *request) nogil
    int httpmorph_request_add_header(httpmorph_request_t *request, const char *key, const char *value) nogil
    int httpmorph_request_set_body(httpmorph_request_t *request, const uint8_t *body, size_t body_len) nogil
    void httpmorph_request_set_timeout(httpmorph_request_t *request, uint32_t timeout_ms) nogil
    void httpmorph_request_set_proxy(httpmorph_request_t *request, const char *proxy_url, const char *username, const char *password) nogil
    void httpmorph_request_set_http2(httpmorph_request_t *request, bint enabled) nogil
    void httpmorph_request_set_verify_ssl(httpmorph_request_t *request, bint verify) nogil
    void httpmorph_request_set_tls_version(httpmorph_request_t *request, uint16_t min_version, uint16_t max_version) nogil
    httpmorph_response* httpmorph_request_execute(httpmorph_client_t *client, const httpmorph_request_t *request, httpmorph_pool_t *pool) nogil
    httpmorph_pool_t* httpmorph_client_get_pool(httpmorph_client_t *client) nogil

    # Response API
    void httpmorph_response_destroy(httpmorph_response *response) nogil
    const char* httpmorph_response_get_header(const httpmorph_response *response, const char *key) nogil

    # Session API
    httpmorph_session_t* httpmorph_session_create(httpmorph_browser_t browser_type) nogil
    void httpmorph_session_destroy(httpmorph_session_t *session) nogil
    httpmorph_response* httpmorph_session_request(httpmorph_session_t *session, const httpmorph_request_t *request) nogil
    size_t httpmorph_session_cookie_count(httpmorph_session_t *session) nogil

    # Async I/O API
    int httpmorph_pool_get_connection_fd(httpmorph_pool_t *pool, const char *host, uint16_t port) nogil


# Python classes

# Simple cookie jar wrapper
class CookieJar:
    """Simulates a cookie jar with length"""
    def __init__(self, count):
        self._count = count

    def __len__(self):
        return self._count

    def __repr__(self):
        return f"<CookieJar with {self._count} cookies>"


cdef class Client:
    """High-performance HTTP client with anti-fingerprinting"""
    cdef httpmorph_client_t *_client

    def __cinit__(self):
        self._client = httpmorph_client_create()
        if self._client is NULL:
            raise MemoryError("Failed to create HTTP client")

    def __dealloc__(self):
        if self._client is not NULL:
            httpmorph_client_destroy(self._client)

    def load_ca_file(self, str ca_file):
        """Load CA certificates from a file (PEM format)

        Args:
            ca_file: Path to CA certificate bundle (e.g., from certifi.where())

        Returns:
            True on success, False on failure
        """
        cdef bytes ca_file_bytes = ca_file.encode('utf-8')
        cdef int result = httpmorph_client_load_ca_file(self._client, ca_file_bytes)
        return result == 0

    def request(self, str method, str url, dict headers=None, bytes body=None, **kwargs):
        """Execute an HTTP request

        Args:
            method: HTTP method (GET, POST, etc.)
            url: URL to request
            headers: Optional dict of headers
            body: Optional request body
            **kwargs: Optional parameters including:
                - timeout: Timeout in seconds (default: 30)
                - proxy: Proxy URL or dict
                - proxy_auth: (username, password) tuple
        """
        cdef httpmorph_method_t c_method
        cdef httpmorph_request_t *req
        cdef httpmorph_response *resp
        cdef const char* c_username
        cdef const char* c_password
        cdef httpmorph_pool_t* client_pool

        # Convert method string to enum
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
        req = httpmorph_request_create(c_method, url_bytes)
        if req is NULL:
            raise MemoryError("Failed to create request")

        try:
            # Set timeout if provided (default is 30 seconds in C code)
            timeout = kwargs.get('timeout')
            if timeout is not None:
                # Convert seconds to milliseconds
                timeout_ms = int(timeout * 1000) if isinstance(timeout, float) else int(timeout) * 1000
                httpmorph_request_set_timeout(req, timeout_ms)

            # Set HTTP/2 flag if provided (default is False)
            http2 = kwargs.get('http2', False)
            httpmorph_request_set_http2(req, http2)

            # Set SSL verification (default is True)
            verify_ssl = kwargs.get('verify', kwargs.get('verify_ssl', True))
            httpmorph_request_set_verify_ssl(req, verify_ssl)

            # Set TLS version range if provided
            tls_version = kwargs.get('tls_version')
            if tls_version:
                if isinstance(tls_version, str):
                    # Map string version to hex value
                    version_map = {
                        '1.0': 0x0301, '1.1': 0x0302, '1.2': 0x0303, '1.3': 0x0304,
                        'TLS1.0': 0x0301, 'TLS1.1': 0x0302, 'TLS1.2': 0x0303, 'TLS1.3': 0x0304,
                    }
                    tls_hex = version_map.get(tls_version, 0)
                    httpmorph_request_set_tls_version(req, tls_hex, tls_hex)
                elif isinstance(tls_version, tuple) and len(tls_version) == 2:
                    min_ver, max_ver = tls_version
                    httpmorph_request_set_tls_version(req, min_ver, max_ver)

            # Set proxy if provided
            proxy = kwargs.get('proxy') or kwargs.get('proxies')
            proxy_auth = kwargs.get('proxy_auth')

            if proxy:
                if isinstance(proxy, dict):
                    # Handle proxies dict like requests library: {'http': 'http://...', 'https': 'http://...'}
                    proxy_url = proxy.get('https') if url.startswith('https') else proxy.get('http')
                    if proxy_url:
                        proxy = proxy_url

                if isinstance(proxy, str):
                    # Extract username/password from proxy_auth
                    c_username = NULL
                    c_password = NULL

                    proxy_bytes = proxy.encode('utf-8')
                    username_bytes = None
                    password_bytes = None

                    if proxy_auth and isinstance(proxy_auth, tuple) and len(proxy_auth) == 2:
                        username, password = proxy_auth
                        if username:
                            username_bytes = username.encode('utf-8')
                            c_username = <const char*>username_bytes
                        if password:
                            password_bytes = password.encode('utf-8')
                            c_password = <const char*>password_bytes

                    httpmorph_request_set_proxy(req, <const char*>proxy_bytes, c_username, c_password)

            # Build request headers dict for tracking
            request_headers = {}
            if headers:
                request_headers.update(headers)

            # Add default headers that will be added by C code if not present
            if 'User-Agent' not in request_headers:
                request_headers['User-Agent'] = 'httpmorph/0.1.3'
            if 'Accept' not in request_headers:
                request_headers['Accept'] = '*/*'
            if 'Connection' not in request_headers:
                request_headers['Connection'] = 'close'

            # Add headers
            if headers:
                for key, value in headers.items():
                    key_bytes = key.encode('utf-8')
                    value_bytes = value.encode('utf-8')
                    httpmorph_request_add_header(req, key_bytes, value_bytes)

            # Set body if present
            if body:
                httpmorph_request_set_body(req, <const uint8_t*>body, len(body))

            # Execute request (release GIL to allow other Python threads to run)
            # Use client's connection pool for reuse
            client_pool = httpmorph_client_get_pool(self._client)
            with nogil:
                resp = httpmorph_request_execute(self._client, req, client_pool)
            if resp is NULL:
                raise RuntimeError("Failed to execute request")

            # Convert response to Python dict
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
                'request_headers': request_headers,
            }

            # Convert headers (use latin-1 per HTTP spec, fallback to utf-8)
            for i in range(resp.header_count):
                key = resp.headers[i].key.decode('latin-1')
                try:
                    value = resp.headers[i].value.decode('latin-1')
                except:
                    value = resp.headers[i].value.decode('utf-8', errors='replace')
                result['headers'][key] = value

            # Cleanup response
            httpmorph_response_destroy(resp)

            return result

        finally:
            httpmorph_request_destroy(req)

    def get_connection_fd(self, str host, int port):
        """Get file descriptor from connection pool for event loop integration

        Returns the underlying socket file descriptor for a pooled connection.
        This enables integration with async event loops (asyncio.add_reader/add_writer).

        Args:
            host: Target hostname
            port: Target port number

        Returns:
            int: File descriptor (>= 0) on success, -1 if no active connection found

        Example:
            >>> client = Client()
            >>> response = client.request('GET', 'https://example.com')
            >>> fd = client.get_connection_fd('example.com', 443)
            >>> if fd >= 0:
            >>>     print(f"Socket FD: {fd}")
        """
        cdef httpmorph_pool_t* pool = httpmorph_client_get_pool(self._client)
        if pool is NULL:
            return -1

        host_bytes = host.encode('utf-8')
        cdef int fd = httpmorph_pool_get_connection_fd(pool, <const char*>host_bytes, port)
        return fd


cdef class Session:
    """HTTP session with persistent fingerprint"""
    cdef httpmorph_session_t *_session
    cdef str _browser

    def __cinit__(self, str browser="chrome"):
        cdef httpmorph_browser_t browser_type

        browser_lower = browser.lower()
        self._browser = browser_lower
        if browser_lower == "chrome":
            browser_type = HTTPMORPH_BROWSER_CHROME
        elif browser_lower == "firefox":
            browser_type = HTTPMORPH_BROWSER_FIREFOX
        elif browser_lower == "safari":
            browser_type = HTTPMORPH_BROWSER_SAFARI
        elif browser_lower == "edge":
            browser_type = HTTPMORPH_BROWSER_EDGE
        elif browser_lower == "random":
            browser_type = HTTPMORPH_BROWSER_RANDOM
        else:
            browser_type = HTTPMORPH_BROWSER_CHROME

        self._session = httpmorph_session_create(browser_type)
        if self._session is NULL:
            raise MemoryError("Failed to create HTTP session")

    def __dealloc__(self):
        if self._session is not NULL:
            httpmorph_session_destroy(self._session)

    def get(self, str url, **kwargs):
        """Execute a GET request"""
        return self.request("GET", url, **kwargs)

    def post(self, str url, **kwargs):
        """Execute a POST request"""
        return self.request("POST", url, **kwargs)

    @property
    def cookie_jar(self):
        """Get cookie count (simulates cookie jar interface)"""
        if self._session is NULL:
            return []
        cdef size_t count
        with nogil:
            count = httpmorph_session_cookie_count(self._session)
        # Return a list-like object with length for compatibility
        return CookieJar(count)

    def request(self, str method, str url, **kwargs):
        """Execute an HTTP request within this session

        Args:
            method: HTTP method (GET, POST, etc.)
            url: URL to request
            **kwargs: Optional parameters including:
                - timeout: Timeout in seconds (default: 30)
                - headers: Dict of headers
                - json: Dict to send as JSON
                - data/body: Request body
        """
        cdef httpmorph_method_t c_method
        cdef httpmorph_request_t *req
        cdef httpmorph_response *resp
        cdef const char* c_username
        cdef const char* c_password

        # Convert method string to enum
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
        req = httpmorph_request_create(c_method, url_bytes)
        if req is NULL:
            raise MemoryError("Failed to create request")

        try:
            # Set timeout if provided (default is 30 seconds in C code)
            timeout = kwargs.get('timeout')
            if timeout is not None:
                # Convert seconds to milliseconds
                timeout_ms = int(timeout * 1000) if isinstance(timeout, float) else int(timeout) * 1000
                httpmorph_request_set_timeout(req, timeout_ms)

            # Set HTTP/2 flag if provided (default is False)
            http2 = kwargs.get('http2', False)
            httpmorph_request_set_http2(req, http2)

            # Set SSL verification (default is True)
            verify_ssl = kwargs.get('verify', kwargs.get('verify_ssl', True))
            httpmorph_request_set_verify_ssl(req, verify_ssl)

            # Set TLS version range if provided
            tls_version = kwargs.get('tls_version')
            if tls_version:
                if isinstance(tls_version, str):
                    # Map string version to hex value
                    version_map = {
                        '1.0': 0x0301, '1.1': 0x0302, '1.2': 0x0303, '1.3': 0x0304,
                        'TLS1.0': 0x0301, 'TLS1.1': 0x0302, 'TLS1.2': 0x0303, 'TLS1.3': 0x0304,
                    }
                    tls_hex = version_map.get(tls_version, 0)
                    httpmorph_request_set_tls_version(req, tls_hex, tls_hex)
                elif isinstance(tls_version, tuple) and len(tls_version) == 2:
                    min_ver, max_ver = tls_version
                    httpmorph_request_set_tls_version(req, min_ver, max_ver)

            # Set proxy if provided
            proxy = kwargs.get('proxy') or kwargs.get('proxies')
            proxy_auth = kwargs.get('proxy_auth')

            if proxy:
                if isinstance(proxy, dict):
                    # Handle proxies dict like requests library
                    proxy_url = proxy.get('https') if url.startswith('https') else proxy.get('http')
                    if proxy_url:
                        proxy = proxy_url

                if isinstance(proxy, str):
                    # Extract username/password from proxy_auth
                    c_username = NULL
                    c_password = NULL

                    proxy_bytes = proxy.encode('utf-8')
                    username_bytes = None
                    password_bytes = None

                    if proxy_auth and isinstance(proxy_auth, tuple) and len(proxy_auth) == 2:
                        username, password = proxy_auth
                        if username:
                            username_bytes = username.encode('utf-8')
                            c_username = <const char*>username_bytes
                        if password:
                            password_bytes = password.encode('utf-8')
                            c_password = <const char*>password_bytes

                    httpmorph_request_set_proxy(req, <const char*>proxy_bytes, c_username, c_password)

            # Add headers
            headers = kwargs.get('headers')
            if headers:
                for key, value in headers.items():
                    key_bytes = key.encode('utf-8')
                    value_bytes = value.encode('utf-8')
                    httpmorph_request_add_header(req, key_bytes, value_bytes)

            # Set body if present
            body = kwargs.get('data') or kwargs.get('body')

            # Handle JSON parameter
            json_data = kwargs.get('json')
            if json_data:
                import json
                body = json.dumps(json_data).encode('utf-8')
                # Add Content-Type header if not already present
                if headers is None or 'Content-Type' not in headers:
                    httpmorph_request_add_header(req, b'Content-Type', b'application/json')

            # Handle form data (dict)
            if body and isinstance(body, dict):
                from urllib.parse import urlencode
                body = urlencode(body).encode('utf-8')
                # Add Content-Type header if not already present
                if headers is None or 'Content-Type' not in headers:
                    httpmorph_request_add_header(req, b'Content-Type', b'application/x-www-form-urlencoded')

            if body:
                if isinstance(body, str):
                    body = body.encode('utf-8')
                httpmorph_request_set_body(req, <const uint8_t*>body, len(body))

            # Build request headers dict for tracking
            request_headers = {}
            if headers:
                request_headers.update(headers)
            # Add JSON content type if it was added
            if json_data and (headers is None or 'Content-Type' not in headers):
                request_headers['Content-Type'] = 'application/json'

            # Get browser-specific User-Agent
            browser_user_agents = {
                'chrome': 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36',
                'firefox': 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:122.0) Gecko/20100101 Firefox/122.0',
                'safari': 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Safari/605.1.15',
                'edge': 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36 Edg/122.0.0.0',
            }
            default_ua = browser_user_agents.get(self._browser, 'httpmorph/0.1.3')

            # Add browser-specific User-Agent header if not already set
            has_user_agent = False
            if headers:
                for key in headers:
                    if key.lower() == 'user-agent':
                        has_user_agent = True
                        break

            if not has_user_agent:
                httpmorph_request_add_header(req, b'User-Agent', default_ua.encode('utf-8'))
                request_headers['User-Agent'] = default_ua

            # Add default headers that will be added by C code if not present
            if 'Accept' not in request_headers:
                request_headers['Accept'] = '*/*'
            if 'Connection' not in request_headers:
                request_headers['Connection'] = 'close'

            # Execute request via session (release GIL to allow other Python threads to run)
            with nogil:
                resp = httpmorph_session_request(self._session, req)
            if resp is NULL:
                raise RuntimeError("Failed to execute request")

            # Convert response to Python dict
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
                'request_headers': request_headers,
            }

            # Convert headers (use latin-1 per HTTP spec, fallback to utf-8)
            for i in range(resp.header_count):
                key = resp.headers[i].key.decode('latin-1')
                try:
                    value = resp.headers[i].value.decode('latin-1')
                except:
                    value = resp.headers[i].value.decode('utf-8', errors='replace')
                result['headers'][key] = value

            # Cleanup response
            httpmorph_response_destroy(resp)

            return result

        finally:
            httpmorph_request_destroy(req)


# Module initialization
def init():
    """Initialize the httpmorph library"""
    cdef int result = httpmorph_init()
    if result != 0:
        raise RuntimeError("Failed to initialize httpmorph library")


def cleanup():
    """Cleanup the httpmorph library"""
    httpmorph_cleanup()


def version():
    """Get library version string"""
    cdef const char* ver = httpmorph_version()
    if ver is NULL:
        return "unknown"
    return ver.decode('utf-8')


# Convenience functions (will be exposed at module level)

def get(str url, **kwargs):
    """Execute a GET request (convenience function)"""
    session = Session(browser=kwargs.get("browser", "chrome"))
    return session.get(url, **kwargs)


def post(str url, **kwargs):
    """Execute a POST request (convenience function)"""
    session = Session(browser=kwargs.get("browser", "chrome"))
    return session.post(url, **kwargs)
