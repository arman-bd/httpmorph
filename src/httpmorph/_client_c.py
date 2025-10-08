"""
C-based HTTP client implementation using Cython bindings
"""

try:
    from httpmorph import _httpmorph
    HAS_C_EXTENSION = True
except ImportError:
    HAS_C_EXTENSION = False
    _httpmorph = None


class Response:
    """HTTP Response object wrapping C response"""

    def __init__(self, c_response_dict):
        self.status_code = c_response_dict['status_code']
        self.headers = c_response_dict['headers']
        self.body = c_response_dict['body']
        self.http_version = self._format_http_version(c_response_dict['http_version'])

        # Timing information (in microseconds)
        self.connect_time_us = c_response_dict['connect_time_us']
        self.tls_time_us = c_response_dict['tls_time_us']
        self.first_byte_time_us = c_response_dict['first_byte_time_us']
        self.total_time_us = c_response_dict['total_time_us']

        # TLS information
        self.tls_version = c_response_dict['tls_version']
        self.tls_cipher = c_response_dict['tls_cipher']
        self.ja3_fingerprint = c_response_dict['ja3_fingerprint']

        # Decode body as text
        try:
            self.text = self.body.decode('utf-8')
        except (UnicodeDecodeError, AttributeError):
            self.text = self.body.decode('latin-1', errors='replace')

        # Error information
        self.error = c_response_dict['error']
        self.error_message = c_response_dict['error_message']

        # Request headers
        self.request_headers = c_response_dict.get('request_headers', {})

    def _format_http_version(self, version_enum):
        """Convert HTTP version enum to string"""
        version_map = {
            0: "1.0",
            1: "1.1",
            2: "2.0",
            3: "3.0",
        }
        return version_map.get(version_enum, "1.1")


class Client:
    """HTTP client using C implementation"""

    def __init__(self):
        if not HAS_C_EXTENSION:
            raise RuntimeError("C extension not available")
        self._client = _httpmorph.Client()

    def request(self, method, url, **kwargs):
        """Execute an HTTP request"""
        headers = kwargs.get('headers', {})
        data = kwargs.get('data') or kwargs.get('body')

        # Convert data to bytes if needed
        if data and isinstance(data, str):
            data = data.encode('utf-8')

        result = self._client.request(method, url, headers=headers, body=data)
        return Response(result)

    def get(self, url, **kwargs):
        """Execute a GET request"""
        return self.request("GET", url, **kwargs)

    def post(self, url, **kwargs):
        """Execute a POST request"""
        return self.request("POST", url, **kwargs)

    def put(self, url, **kwargs):
        """Execute a PUT request"""
        return self.request("PUT", url, **kwargs)

    def delete(self, url, **kwargs):
        """Execute a DELETE request"""
        return self.request("DELETE", url, **kwargs)


class Session:
    """HTTP session with persistent fingerprint"""

    def __init__(self, browser="chrome"):
        if not HAS_C_EXTENSION:
            raise RuntimeError("C extension not available")
        self._session = _httpmorph.Session(browser=browser)
        self.browser = browser

    @property
    def cookie_jar(self):
        """Get cookie jar from underlying session"""
        return self._session.cookie_jar

    def request(self, method, url, **kwargs):
        """Execute an HTTP request within this session"""
        result = self._session.request(method, url, **kwargs)
        return Response(result)

    def get(self, url, **kwargs):
        """Execute a GET request"""
        return self.request("GET", url, **kwargs)

    def post(self, url, **kwargs):
        """Execute a POST request"""
        return self.request("POST", url, **kwargs)

    def put(self, url, **kwargs):
        """Execute a PUT request"""
        return self.request("PUT", url, **kwargs)

    def delete(self, url, **kwargs):
        """Execute a DELETE request"""
        return self.request("DELETE", url, **kwargs)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        pass


# Module-level convenience functions
_default_session = None

def get_default_session():
    """Get or create default session"""
    global _default_session
    if _default_session is None:
        _default_session = Session()
    return _default_session


def get(url, **kwargs):
    """Execute a GET request using default session"""
    return get_default_session().get(url, **kwargs)


def post(url, **kwargs):
    """Execute a POST request using default session"""
    return get_default_session().post(url, **kwargs)


def put(url, **kwargs):
    """Execute a PUT request using default session"""
    return get_default_session().put(url, **kwargs)


def delete(url, **kwargs):
    """Execute a DELETE request using default session"""
    return get_default_session().delete(url, **kwargs)


def init():
    """Initialize the httpmorph library"""
    if HAS_C_EXTENSION:
        _httpmorph.init()


def cleanup():
    """Cleanup the httpmorph library"""
    global _default_session
    _default_session = None
    if HAS_C_EXTENSION:
        _httpmorph.cleanup()


def version():
    """Get library version"""
    if HAS_C_EXTENSION:
        return _httpmorph.version()
    return "0.1.0"
