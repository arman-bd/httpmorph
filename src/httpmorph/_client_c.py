"""
C-based HTTP client implementation using Cython bindings
"""

import base64
import io
import json as _json
import uuid
from datetime import timedelta
from http.client import responses as http_responses
from urllib.parse import parse_qs, urlencode, urlparse, urlunparse

try:
    from httpmorph import _httpmorph

    HAS_C_EXTENSION = True
except ImportError as e:
    import sys
    print(f"WARNING: Failed to import _httpmorph: {e}", file=sys.stderr)
    import traceback
    traceback.print_exc()
    HAS_C_EXTENSION = False
    _httpmorph = None


# Exception classes (requests-compatible)
class RequestException(Exception):
    """Base exception for all request exceptions"""

    pass


class HTTPError(RequestException):
    """HTTP error occurred"""

    def __init__(self, *args, response=None, **kwargs):
        super().__init__(*args, **kwargs)
        self.response = response


class ConnectionError(RequestException):
    """Connection error occurred"""

    pass


class Timeout(RequestException):
    """Request timed out"""

    pass


class TooManyRedirects(RequestException):
    """Too many redirects"""

    pass


# Request classes (requests-compatible)
class Request:
    """HTTP Request object"""

    def __init__(self, method, url, **kwargs):
        self.method = method.upper()
        self.url = url
        self.headers = kwargs.get("headers", {})
        self.data = kwargs.get("data")
        self.json = kwargs.get("json")
        self.params = kwargs.get("params")
        self.files = kwargs.get("files")
        self.auth = kwargs.get("auth")
        self.cookies = kwargs.get("cookies")
        self.hooks = kwargs.get("hooks")

    def prepare(self):
        """Prepare the request"""
        p = PreparedRequest()
        p.prepare(
            method=self.method,
            url=self.url,
            headers=self.headers,
            data=self.data,
            json=self.json,
            params=self.params,
            files=self.files,
            auth=self.auth,
            cookies=self.cookies,
            hooks=self.hooks,
        )
        return p


class PreparedRequest:
    """Prepared HTTP request"""

    def __init__(self):
        self.method = None
        self.url = None
        self.headers = {}
        self.body = None

    def prepare(
        self,
        method=None,
        url=None,
        headers=None,
        data=None,
        json=None,
        params=None,
        files=None,
        auth=None,
        cookies=None,
        hooks=None,
    ):
        """Prepare the request"""
        self.method = method
        self.url = url
        self.headers = headers or {}

        if json is not None:
            self.body = _json.dumps(json).encode("utf-8")
            self.headers["Content-Type"] = "application/json"
        elif data is not None:
            self.body = data if isinstance(data, bytes) else str(data).encode("utf-8")
        else:
            self.body = None


class Response:
    """HTTP Response object wrapping C response"""

    def __init__(self, c_response_dict, url=None):
        self.status_code = c_response_dict["status_code"]
        self.headers = c_response_dict["headers"]
        self.body = c_response_dict["body"]
        self.http_version = self._format_http_version(c_response_dict["http_version"])

        # Timing information (in microseconds)
        self.connect_time_us = c_response_dict["connect_time_us"]
        self.tls_time_us = c_response_dict["tls_time_us"]
        self.first_byte_time_us = c_response_dict["first_byte_time_us"]
        self.total_time_us = c_response_dict["total_time_us"]

        # TLS information
        self.tls_version = c_response_dict["tls_version"]
        self.tls_cipher = c_response_dict["tls_cipher"]
        self.ja3_fingerprint = c_response_dict["ja3_fingerprint"]

        # Decode body as text
        self._encoding = None
        try:
            self.text = self.body.decode("utf-8")
        except (UnicodeDecodeError, AttributeError):
            self.text = self.body.decode("latin-1", errors="replace")

        # Error information
        self.error = c_response_dict["error"]
        self.error_message = c_response_dict["error_message"]

        # Request headers
        self.request_headers = c_response_dict.get("request_headers", {})

        # Requests-compatible attributes
        self.url = url or c_response_dict.get("url", "")
        self.history = []
        self.raw = io.BytesIO(self.body) if self.body else io.BytesIO()
        self.links = {}

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
    def content(self):
        """Alias for body (requests compatibility)"""
        return self.body

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
        # Convert microseconds to seconds
        seconds = self.total_time_us / 1_000_000.0
        return timedelta(seconds=seconds)

    @property
    def encoding(self):
        """Get the encoding from Content-Type header or manual override"""
        if self._encoding:
            return self._encoding

        # Try to detect from Content-Type header
        content_type = self.headers.get("content-type", "") or self.headers.get("Content-Type", "")
        if "charset=" in content_type:
            return content_type.split("charset=")[-1].split(";")[0].strip()
        return None

    @encoding.setter
    def encoding(self, value):
        """Set encoding manually"""
        self._encoding = value

    @property
    def apparent_encoding(self):
        """Detect encoding from content (simplified)"""
        # Simple detection - could be enhanced with chardet
        return "utf-8"

    def json(self, **kwargs):
        """Parse response body as JSON"""
        return _json.loads(self.text, **kwargs)

    def raise_for_status(self):
        """Raise HTTPError if status code indicates an error"""
        if 400 <= self.status_code < 600:
            raise HTTPError(f"{self.status_code} Error: {self.reason}", response=self)
        return self  # Return self for method chaining

    def iter_content(self, chunk_size=1, decode_unicode=False):
        """Iterate over response content in chunks"""
        content = self.body if self.body else b""

        if decode_unicode and isinstance(content, bytes):
            content = content.decode(self.encoding or "utf-8")

        for i in range(0, len(content), chunk_size):
            yield content[i : i + chunk_size]

    def iter_lines(self, chunk_size=512, decode_unicode=False, delimiter=None):
        """Iterate over response lines"""
        content = self.body if self.body else b""

        if decode_unicode and isinstance(content, bytes):
            content = content.decode(self.encoding or "utf-8")
            lines = content.splitlines()
        elif isinstance(content, bytes):
            if delimiter is None:
                delimiter = b"\n"
            lines = content.split(delimiter)
        else:
            lines = content.splitlines()

        for line in lines:
            if line:  # Skip empty lines
                yield line


class Client:
    """HTTP client using C implementation"""

    def __init__(self):
        if not HAS_C_EXTENSION:
            raise RuntimeError("C extension not available")
        self._client = _httpmorph.Client()

    def request(self, method, url, **kwargs):
        """Execute an HTTP request"""
        # Handle params - append query parameters to URL
        if "params" in kwargs:
            params = kwargs.pop("params")
            if params:
                # Parse existing URL
                parsed = urlparse(url)
                # Parse existing query params
                query_params = parse_qs(parsed.query, keep_blank_values=True)

                # Add new params (skip None values like requests does)
                for key, value in params.items():
                    if value is not None:
                        if isinstance(value, list):
                            query_params[key] = value
                        else:
                            query_params[key] = [str(value)]

                # Rebuild query string
                query_string = urlencode(query_params, doseq=True)

                # Rebuild URL
                url = urlunparse(
                    (
                        parsed.scheme,
                        parsed.netloc,
                        parsed.path,
                        parsed.params,
                        query_string,
                        parsed.fragment,
                    )
                )

        # Handle auth - convert to Authorization header
        if "auth" in kwargs:
            auth = kwargs.pop("auth")
            if auth:
                username, password = auth
                credentials = f"{username}:{password}".encode()
                encoded = base64.b64encode(credentials).decode("ascii")
                if "headers" not in kwargs:
                    kwargs["headers"] = {}
                kwargs["headers"]["Authorization"] = f"Basic {encoded}"

        # Handle cookies - convert to Cookie header
        if "cookies" in kwargs:
            cookies = kwargs.pop("cookies")
            if cookies:
                cookie_str = "; ".join([f"{k}={v}" for k, v in cookies.items()])
                if "headers" not in kwargs:
                    kwargs["headers"] = {}
                kwargs["headers"]["Cookie"] = cookie_str

        # Handle redirects (default: follow redirects)
        allow_redirects = kwargs.pop("allow_redirects", True)
        max_redirects = kwargs.pop("max_redirects", 10)

        # Handle timeout tuple (connect_timeout, read_timeout)
        if "timeout" in kwargs:
            timeout = kwargs["timeout"]
            if isinstance(timeout, tuple):
                # For simplicity, use the max of connect and read timeout
                # In requests, timeout=(connect, read)
                kwargs["timeout"] = max(timeout)

        # Handle files parameter - create multipart/form-data
        if "files" in kwargs:
            files = kwargs.pop("files")
            # Get form data if present
            form_data = kwargs.pop("data", {})

            if files:
                boundary = uuid.uuid4().hex
                body_parts = []

                # Add form data fields first
                if form_data and isinstance(form_data, dict):
                    for name, value in form_data.items():
                        part = f"--{boundary}\r\n"
                        part += f'Content-Disposition: form-data; name="{name}"\r\n\r\n'
                        part += f"{value}\r\n"
                        body_parts.append(part.encode("utf-8"))

                # Add files
                for name, file_info in files.items():
                    if isinstance(file_info, tuple):
                        filename, file_content = file_info[0], file_info[1]
                    else:
                        filename = name
                        file_content = file_info

                    # Read file content if it's a file-like object
                    if hasattr(file_content, "read"):
                        file_content = file_content.read()

                    # Convert to bytes if needed
                    if isinstance(file_content, str):
                        file_content = file_content.encode("utf-8")

                    part = f"--{boundary}\r\n"
                    part += (
                        f'Content-Disposition: form-data; name="{name}"; filename="{filename}"\r\n'
                    )
                    part += "Content-Type: application/octet-stream\r\n\r\n"
                    body_parts.append(part.encode("utf-8"))
                    body_parts.append(file_content)
                    body_parts.append(b"\r\n")

                body_parts.append(f"--{boundary}--\r\n".encode())
                kwargs["data"] = b"".join(body_parts)

                if "headers" not in kwargs:
                    kwargs["headers"] = {}
                kwargs["headers"]["Content-Type"] = f"multipart/form-data; boundary={boundary}"

        # Make initial request
        result = self._client.request(method, url, **kwargs)

        # Check for errors and raise appropriate exceptions
        if result.get("error"):
            error_code = result["error"]
            error_msg = result.get("error_message", "Request failed")

            # Map C error codes to Python exceptions
            # HTTPMORPH_ERROR_TIMEOUT = 5
            if error_code == 5:
                raise Timeout(error_msg)
            # HTTPMORPH_ERROR_NETWORK = 3
            elif error_code == 3:
                raise ConnectionError(error_msg)
            # Other errors
            elif error_code != 0:
                raise RequestException(error_msg)

        response = Response(result, url=url)

        # Follow redirects if needed
        if allow_redirects:
            redirect_count = 0
            history = []

            while response.is_redirect and redirect_count < max_redirects:
                # Save current response to history
                history.append(response)

                # Get redirect location
                location = response.headers.get("Location") or response.headers.get("location")
                if not location:
                    break

                # Handle relative URLs
                if not location.startswith(("http://", "https://")):
                    parsed = urlparse(url)
                    if location.startswith("/"):
                        # Absolute path
                        location = f"{parsed.scheme}://{parsed.netloc}{location}"
                    else:
                        # Relative path
                        base_path = parsed.path.rsplit("/", 1)[0] if "/" in parsed.path else ""
                        location = f"{parsed.scheme}://{parsed.netloc}{base_path}/{location}"

                # Update URL for next request
                url = location
                redirect_count += 1

                # For 302 and 303, convert POST to GET (per HTTP spec and common practice)
                if response.status_code in (302, 303) and method == "POST":
                    method = "GET"
                    if "data" in kwargs:
                        kwargs.pop("data")
                    if "json" in kwargs:
                        kwargs.pop("json")

                # Make redirect request
                result = self._client.request(method, url, **kwargs)

                # Check for errors and raise appropriate exceptions
                if result.get("error"):
                    error_code = abs(result["error"])
                    error_msg = result.get("error_message", "Request failed")
                    if error_code == 5:
                        raise Timeout(error_msg)
                    elif error_code == 3:
                        raise ConnectionError(error_msg)
                    elif error_code != 0:
                        raise RequestException(error_msg)

                response = Response(result, url=url)

                # Parse Set-Cookie headers from redirect response
                if "Set-Cookie" in response.headers or "set-cookie" in response.headers:
                    set_cookie = response.headers.get("Set-Cookie") or response.headers.get(
                        "set-cookie"
                    )
                    self._cookies.parse_set_cookie(set_cookie)

            if redirect_count >= max_redirects and response.is_redirect:
                raise TooManyRedirects(f"Exceeded {max_redirects} redirects")

            # Set history on final response
            response.history = history

        return response

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

    def head(self, url, **kwargs):
        """Execute a HEAD request"""
        return self.request("HEAD", url, **kwargs)

    def patch(self, url, **kwargs):
        """Execute a PATCH request"""
        return self.request("PATCH", url, **kwargs)

    def options(self, url, **kwargs):
        """Execute an OPTIONS request"""
        return self.request("OPTIONS", url, **kwargs)


class CookieDict(dict):
    """Dict-like wrapper for cookie jar with Set-Cookie parsing"""

    def __init__(self, c_cookie_jar=None):
        super().__init__()
        self._c_jar = c_cookie_jar

    def parse_set_cookie(self, set_cookie_header):
        """Parse Set-Cookie header and add to dict"""
        if not set_cookie_header:
            return

        # Simple parsing - just get name=value part
        parts = set_cookie_header.split(";")
        if parts:
            cookie_part = parts[0].strip()
            if "=" in cookie_part:
                name, value = cookie_part.split("=", 1)
                self[name] = value


class Session:
    """HTTP session with persistent fingerprint"""

    def __init__(self, browser="chrome"):
        if not HAS_C_EXTENSION:
            raise RuntimeError("C extension not available")
        self._session = _httpmorph.Session(browser=browser)
        self.browser = browser
        self.headers = {}  # Persistent headers
        self._cookies = CookieDict(self._session.cookie_jar)

    @property
    def cookie_jar(self):
        """Get cookie jar from underlying session"""
        return self._session.cookie_jar

    @property
    def cookies(self):
        """Get dict-like cookie container (requests compatibility)"""
        return self._cookies

    def request(self, method, url, **kwargs):
        """Execute an HTTP request within this session"""
        # Handle params - append query parameters to URL
        if "params" in kwargs:
            params = kwargs.pop("params")
            if params:
                # Parse existing URL
                parsed = urlparse(url)
                # Parse existing query params
                query_params = parse_qs(parsed.query, keep_blank_values=True)

                # Add new params (skip None values like requests does)
                for key, value in params.items():
                    if value is not None:
                        if isinstance(value, list):
                            query_params[key] = value
                        else:
                            query_params[key] = [str(value)]

                # Rebuild query string
                query_string = urlencode(query_params, doseq=True)

                # Rebuild URL
                url = urlunparse(
                    (
                        parsed.scheme,
                        parsed.netloc,
                        parsed.path,
                        parsed.params,
                        query_string,
                        parsed.fragment,
                    )
                )

        # Merge session headers with request headers
        headers = self.headers.copy()
        if kwargs.get("headers"):
            request_headers = kwargs["headers"]
            if not isinstance(request_headers, dict):
                raise TypeError("headers must be a dictionary")
            headers.update(request_headers)

        # Handle auth - convert to Authorization header
        if "auth" in kwargs:
            auth = kwargs.pop("auth")
            if auth:
                username, password = auth
                credentials = f"{username}:{password}".encode()
                encoded = base64.b64encode(credentials).decode("ascii")
                headers["Authorization"] = f"Basic {encoded}"

        # Handle cookies - convert to Cookie header
        if "cookies" in kwargs:
            cookies = kwargs.pop("cookies")
            if cookies:
                cookie_str = "; ".join([f"{k}={v}" for k, v in cookies.items()])
                headers["Cookie"] = cookie_str

        kwargs["headers"] = headers

        # Handle redirects (default: follow redirects)
        allow_redirects = kwargs.pop("allow_redirects", True)
        max_redirects = kwargs.pop("max_redirects", 10)

        # Handle timeout tuple (connect_timeout, read_timeout)
        if "timeout" in kwargs:
            timeout = kwargs["timeout"]
            if isinstance(timeout, tuple):
                # For simplicity, use the max of connect and read timeout
                # In requests, timeout=(connect, read)
                kwargs["timeout"] = max(timeout)

        # Handle files parameter - create multipart/form-data
        if "files" in kwargs:
            files = kwargs.pop("files")
            # Get form data if present
            form_data = kwargs.pop("data", {})

            if files:
                boundary = uuid.uuid4().hex
                body_parts = []

                # Add form data fields first
                if form_data and isinstance(form_data, dict):
                    for name, value in form_data.items():
                        part = f"--{boundary}\r\n"
                        part += f'Content-Disposition: form-data; name="{name}"\r\n\r\n'
                        part += f"{value}\r\n"
                        body_parts.append(part.encode("utf-8"))

                # Add files
                for name, file_info in files.items():
                    if isinstance(file_info, tuple):
                        filename, file_content = file_info[0], file_info[1]
                    else:
                        filename = name
                        file_content = file_info

                    # Read file content if it's a file-like object
                    if hasattr(file_content, "read"):
                        file_content = file_content.read()

                    # Convert to bytes if needed
                    if isinstance(file_content, str):
                        file_content = file_content.encode("utf-8")

                    part = f"--{boundary}\r\n"
                    part += (
                        f'Content-Disposition: form-data; name="{name}"; filename="{filename}"\r\n'
                    )
                    part += "Content-Type: application/octet-stream\r\n\r\n"
                    body_parts.append(part.encode("utf-8"))
                    body_parts.append(file_content)
                    body_parts.append(b"\r\n")

                body_parts.append(f"--{boundary}--\r\n".encode())
                kwargs["data"] = b"".join(body_parts)
                headers["Content-Type"] = f"multipart/form-data; boundary={boundary}"

        kwargs["headers"] = headers

        # Make initial request
        result = self._session.request(method, url, **kwargs)

        # Check for errors and raise appropriate exceptions
        if result.get("error"):
            error_code = abs(result["error"])  # C returns negative error codes
            error_msg = result.get("error_message", "Request failed")
            # HTTPMORPH_ERROR_TIMEOUT = 5
            if error_code == 5:
                raise Timeout(error_msg)
            # HTTPMORPH_ERROR_NETWORK = 3
            elif error_code == 3:
                raise ConnectionError(error_msg)
            # Other errors
            elif error_code != 0:
                raise RequestException(error_msg)

        response = Response(result, url=url)

        # Parse Set-Cookie headers from response
        if "Set-Cookie" in response.headers or "set-cookie" in response.headers:
            set_cookie = response.headers.get("Set-Cookie") or response.headers.get("set-cookie")
            self._cookies.parse_set_cookie(set_cookie)

        # Follow redirects if needed
        if allow_redirects:
            redirect_count = 0
            history = []

            while response.is_redirect and redirect_count < max_redirects:
                # Save current response to history
                history.append(response)

                # Get redirect location
                location = response.headers.get("Location") or response.headers.get("location")
                if not location:
                    break

                # Handle relative URLs
                if not location.startswith(("http://", "https://")):
                    parsed = urlparse(url)
                    if location.startswith("/"):
                        # Absolute path
                        location = f"{parsed.scheme}://{parsed.netloc}{location}"
                    else:
                        # Relative path
                        base_path = parsed.path.rsplit("/", 1)[0] if "/" in parsed.path else ""
                        location = f"{parsed.scheme}://{parsed.netloc}{base_path}/{location}"

                # Update URL for next request
                url = location
                redirect_count += 1

                # For 302 and 303, convert POST to GET (per HTTP spec and common practice)
                if response.status_code in (302, 303) and method == "POST":
                    method = "GET"
                    if "data" in kwargs:
                        kwargs.pop("data")
                    if "json" in kwargs:
                        kwargs.pop("json")

                # Make redirect request
                result = self._session.request(method, url, **kwargs)

                # Check for errors and raise appropriate exceptions
                if result.get("error"):
                    error_code = abs(result["error"])
                    error_msg = result.get("error_message", "Request failed")
                    if error_code == 5:
                        raise Timeout(error_msg)
                    elif error_code == 3:
                        raise ConnectionError(error_msg)
                    elif error_code != 0:
                        raise RequestException(error_msg)

                response = Response(result, url=url)

                # Parse Set-Cookie headers from redirect response
                if "Set-Cookie" in response.headers or "set-cookie" in response.headers:
                    set_cookie = response.headers.get("Set-Cookie") or response.headers.get(
                        "set-cookie"
                    )
                    self._cookies.parse_set_cookie(set_cookie)

            if redirect_count >= max_redirects and response.is_redirect:
                raise TooManyRedirects(f"Exceeded {max_redirects} redirects")

            # Set history on final response
            response.history = history

        return response

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

    def head(self, url, **kwargs):
        """Execute a HEAD request"""
        return self.request("HEAD", url, **kwargs)

    def patch(self, url, **kwargs):
        """Execute a PATCH request"""
        return self.request("PATCH", url, **kwargs)

    def options(self, url, **kwargs):
        """Execute an OPTIONS request"""
        return self.request("OPTIONS", url, **kwargs)

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


def head(url, **kwargs):
    """Execute a HEAD request using default session"""
    return get_default_session().head(url, **kwargs)


def patch(url, **kwargs):
    """Execute a PATCH request using default session"""
    return get_default_session().patch(url, **kwargs)


def options(url, **kwargs):
    """Execute an OPTIONS request using default session"""
    return get_default_session().options(url, **kwargs)


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
