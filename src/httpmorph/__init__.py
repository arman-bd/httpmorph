"""
httpmorph - Morph into any browser

High-performance HTTP/HTTPS client with dynamic browser fingerprinting.

Built from scratch in C with BoringSSL. No fallback implementations.
"""

import sys

# Read version from package metadata (single source of truth: pyproject.toml)
try:
    from importlib.metadata import version as _get_version
except ImportError:
    # Python < 3.8
    from importlib_metadata import version as _get_version

__version__ = _get_version("httpmorph")
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

# Import async client (C-level async I/O with kqueue/epoll)
try:
    from httpmorph._async_client import AsyncClient, AsyncResponse

    HAS_ASYNC = True
except ImportError:
    AsyncClient = None
    AsyncResponse = None
    HAS_ASYNC = False

__all__ = [
    # Sync API
    "Client",
    "Session",
    "Response",
    "Request",
    "PreparedRequest",
    "get",
    "post",
    "put",
    "delete",
    "head",
    "patch",
    "options",
    # Async API
    "AsyncClient",
    "AsyncResponse",
    # Exceptions
    "HTTPError",
    "ConnectionError",
    "Timeout",
    "TooManyRedirects",
    "RequestException",
    # Utilities
    "init",
    "cleanup",
    "version",
    # Feature flags
    "HAS_HTTP2",
    "HAS_ASYNC",
]
