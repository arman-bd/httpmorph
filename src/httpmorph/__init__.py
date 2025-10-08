"""
httpmorph - Morph into any browser

High-performance HTTP/HTTPS client with dynamic browser fingerprinting.

Built from scratch in C with BoringSSL. No fallback implementations.
"""

import sys

__version__ = "0.1.0"
__author__ = "Arman"
__license__ = "MIT"

# Import C implementation (required - no fallback!)
from httpmorph._client_c import (
    HAS_C_EXTENSION,
    Client,
    Response,
    Session,
    cleanup,
    delete,
    get,
    init,
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

__all__ = [
    "Client",
    "Session",
    "Response",
    "get",
    "post",
    "put",
    "delete",
    "init",
    "cleanup",
    "version",
    "HAS_HTTP2",
]
