"""
httpmorph - Morph into any browser

High-performance HTTP/HTTPS client with dynamic browser fingerprinting.

Built from scratch in C with BoringSSL. No fallback implementations.
"""

import sys

__version__ = "0.1.3"
__author__ = "Arman Hossain"
__license__ = "MIT"

# Import C implementation (required - no fallback!)
from httpmorph._client_c import (
    HAS_C_EXTENSION,
    Client,
    ConnectionError,
    HTTPError,
    PreparedRequest,
    Request,
    RequestException,
    Response,
    Session,
    Timeout,
    TooManyRedirects,
    cleanup,
    delete,
    get,
    head,
    init,
    options,
    patch,
    post,
    put,
    version,
)

# Try to import HTTP/2 C extension (optional)
try:
    from httpmorph import _http2  # noqa: F401

    HAS_HTTP2 = True
except ImportError:
    HAS_HTTP2 = False

# Auto-initialize
init()

# Confirm C extension loaded
if not HAS_C_EXTENSION:
    raise RuntimeError(
        "httpmorph C extension failed to load. "
        "Please ensure the package was built correctly with: "
        "python setup.py build_ext --inplace"
    )

print("[httpmorph] Using C extension with BoringSSL", file=sys.stderr)

# Import async support
try:
    from httpmorph._async import AsyncClient
    from httpmorph._async import get as async_get
    from httpmorph._async import post as async_post
    from httpmorph._async import request as async_request
    HAS_ASYNC = True
except ImportError:
    HAS_ASYNC = False
    AsyncClient = None
    async_request = async_get = async_post = None

__all__ = [
    "Client",
    "Session",
    "Response",
    "Request",
    "PreparedRequest",
    "HTTPError",
    "ConnectionError",
    "Timeout",
    "TooManyRedirects",
    "RequestException",
    "get",
    "post",
    "put",
    "delete",
    "head",
    "patch",
    "options",
    "init",
    "cleanup",
    "version",
    "HAS_HTTP2",
    "HAS_ASYNC",
    "AsyncClient",
    "async_request",
    "async_get",
    "async_post",
]
