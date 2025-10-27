"""
Async/await support for httpmorph using Python's asyncio.

This provides a native async API that integrates with asyncio event loops,
giving excellent concurrent performance without threading issues.
"""

import asyncio
from typing import Optional, Dict, Any, TYPE_CHECKING

if TYPE_CHECKING:
    from . import Response

# Import at runtime
import httpmorph


class AsyncClient:
    """Async HTTP client with native asyncio support.

    Uses run_in_executor to run blocking C operations without blocking
    the event loop. For true async, would need non-blocking C sockets.
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
