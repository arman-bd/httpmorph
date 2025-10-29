"""
Async/await support for httpmorph using Python's asyncio.

This provides a native async API using run_in_executor to prevent blocking
the event loop. This approach:
- Runs C operations in a thread pool (non-blocking to event loop)
- Enables excellent concurrent performance
- Avoids SSL threading issues through proper executor usage

The C layer includes non-blocking socket infrastructure (WOULD_BLOCK handling)
which is prepared for future event-loop-based async integration.
"""

import asyncio
from typing import TYPE_CHECKING, Dict, Optional

if TYPE_CHECKING:
    pass

# Import at runtime


class AsyncClient:
    """Async HTTP client with asyncio support.

    Uses asyncio.run_in_executor to run C operations in a thread pool,
    preventing event loop blocking. This provides excellent concurrent
    performance for async/await code.

    The underlying C layer includes non-blocking socket infrastructure
    ready for future event-loop-based async integration.
    """

    def __init__(self, http2: bool = False, timeout: int = 30):
        """Create an async HTTP client.

        Args:
            http2: Enable HTTP/2 support
            timeout: Request timeout in seconds
        """
        # Import Client from the parent package to avoid circular import
        from . import _client_c
        self._client = _client_c.Client(http2=http2)
        self._timeout = timeout

    async def request(
        self,
        method: str,
        url: str,
        *,
        headers: Optional[Dict[str, str]] = None,
        data: Optional[bytes] = None,
        timeout: Optional[int] = None,
    ):
        """Make an async HTTP request.

        Args:
            method: HTTP method (GET, POST, etc.)
            url: Target URL
            headers: Optional request headers
            data: Optional request body
            timeout: Optional request timeout (overrides client default)

        Returns:
            Response object with status, headers, and body

        Example:
            >>> client = AsyncClient()
            >>> response = await client.request('GET', 'https://api.github.com')
            >>> print(response.status_code, response.text)
        """
        loop = asyncio.get_event_loop()
        timeout_val = timeout if timeout is not None else self._timeout

        # Run blocking C operation in thread pool
        # This prevents blocking the event loop
        return await loop.run_in_executor(
            None,
            lambda: self._client.request(
                method,
                url,
                headers=headers,
                data=data,
                timeout=timeout_val
            )
        )

    async def get(self, url: str, **kwargs):
        """Async GET request."""
        return await self.request('GET', url, **kwargs)

    async def post(self, url: str, **kwargs):
        """Async POST request."""
        return await self.request('POST', url, **kwargs)

    async def put(self, url: str, **kwargs):
        """Async PUT request."""
        return await self.request('PUT', url, **kwargs)

    async def delete(self, url: str, **kwargs):
        """Async DELETE request."""
        return await self.request('DELETE', url, **kwargs)

    async def __aenter__(self):
        """Async context manager support."""
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        """Async context manager cleanup."""
        # Client cleanup happens automatically
        return False


async def request(
    method: str,
    url: str,
    *,
    headers: Optional[Dict[str, str]] = None,
    data: Optional[bytes] = None,
    timeout: int = 30,
    http2: bool = False,
):
    """Make a one-off async HTTP request.

    Creates a client, makes the request, and cleans up automatically.
    For multiple requests, use AsyncClient for better performance.

    Args:
        method: HTTP method
        url: Target URL
        headers: Optional headers
        data: Optional body
        timeout: Request timeout
        http2: Enable HTTP/2

    Returns:
        Response object

    Example:
        >>> response = await httpmorph.async_request('GET', 'https://example.com')
        >>> print(response.status_code)
    """
    client = AsyncClient(http2=http2, timeout=timeout)
    return await client.request(method, url, headers=headers, data=data)


async def get(url: str, **kwargs):
    """Async GET request (convenience function)."""
    return await request('GET', url, **kwargs)


async def post(url: str, **kwargs):
    """Async POST request (convenience function)."""
    return await request('POST', url, **kwargs)
