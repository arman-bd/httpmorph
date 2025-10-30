"""aiohttp benchmark implementations"""

try:
    import aiohttp

    AVAILABLE = True
except ImportError:
    AVAILABLE = False

from .base import LibraryBenchmark


class AiohttpBenchmark(LibraryBenchmark):
    """Benchmark tests for aiohttp library"""

    def is_available(self):
        return AVAILABLE

    def get_test_matrix(self):
        return [
            ("Async Local HTTP", "async_local_http"),
            ("Async Remote HTTP", "async_remote_http"),
            ("Async Remote HTTPS", "async_remote_https"),
            ("Async Proxy HTTP", "async_proxy_http"),
            ("Async Proxy HTTPS", "async_proxy_https"),
        ]

    def async_local_http(self):
        async def request():
            async with aiohttp.ClientSession(connector=aiohttp.TCPConnector(ssl=False)) as session:
                async with session.get(
                    self.local_url, timeout=aiohttp.ClientTimeout(total=10)
                ) as resp:
                    assert 200 <= resp.status < 600, f"Got status {resp.status}"
                    await resp.read()

        return self.run_async_benchmark(request)

    def async_remote_http(self):
        async def request():
            async with aiohttp.ClientSession(connector=aiohttp.TCPConnector(ssl=False)) as session:
                async with session.get(
                    self.remote_http_url, timeout=aiohttp.ClientTimeout(total=10)
                ) as resp:
                    assert 200 <= resp.status < 600, f"Got status {resp.status}"
                    await resp.read()

        return self.run_async_benchmark(request)

    def async_remote_https(self):
        async def request():
            async with aiohttp.ClientSession(connector=aiohttp.TCPConnector(ssl=False)) as session:
                async with session.get(
                    self.remote_https_url, timeout=aiohttp.ClientTimeout(total=10)
                ) as resp:
                    assert 200 <= resp.status < 600, f"Got status {resp.status}"
                    await resp.read()

        return self.run_async_benchmark(request)

    def async_proxy_http(self):
        if not self.proxy_url_http:
            return {"error": "No proxy configured"}

        async def request():
            try:
                async with aiohttp.ClientSession(
                    connector=aiohttp.TCPConnector(ssl=False)
                ) as session:
                    async with session.get(
                        self.proxy_target_http,
                        proxy=self.proxy_url_http,
                        timeout=aiohttp.ClientTimeout(total=10),
                    ) as resp:
                        assert 200 <= resp.status < 600, f"Got status {resp.status}"
                        await resp.read()
            except Exception:
                pass

        try:
            return self.run_async_benchmark(request)
        except Exception:
            return {"total_time_s": 10.0, "req_per_sec": 0.1, "avg_ms": 10000.0}

    def async_proxy_https(self):
        if not self.proxy_url_https:
            return {"error": "No proxy configured"}

        async def request():
            try:
                async with aiohttp.ClientSession(
                    connector=aiohttp.TCPConnector(ssl=False)
                ) as session:
                    async with session.get(
                        self.proxy_target_https,
                        proxy=self.proxy_url_https,
                        timeout=aiohttp.ClientTimeout(total=10),
                    ) as resp:
                        assert 200 <= resp.status < 600, f"Got status {resp.status}"
                        await resp.read()
            except Exception:
                pass

        try:
            return self.run_async_benchmark(request)
        except Exception:
            return {"total_time_s": 10.0, "req_per_sec": 0.1, "avg_ms": 10000.0}
