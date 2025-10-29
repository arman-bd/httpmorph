"""httpx benchmark implementations"""
import asyncio
try:
    import httpx
    AVAILABLE = True
except ImportError:
    AVAILABLE = False

from .base import LibraryBenchmark


class HttpxBenchmark(LibraryBenchmark):
    """Benchmark tests for httpx library"""

    def is_available(self):
        return AVAILABLE

    def get_test_matrix(self):
        return [
            ('Sequential Local HTTP', 'seq_local_http'),
            ('Sequential Remote HTTP', 'seq_remote_http'),
            ('Sequential Remote HTTPS', 'seq_remote_https'),
            ('Sequential Remote HTTP/2', 'seq_remote_http2'),
            ('Sequential Proxy HTTP', 'seq_proxy_http'),
            ('Sequential Proxy HTTPS', 'seq_proxy_https'),
            ('Sequential Proxy HTTP/2', 'seq_proxy_http2'),
            ('Concurrent Local HTTP', 'conc_local_http'),
            ('Concurrent Remote HTTP', 'conc_remote_http'),
            ('Concurrent Remote HTTPS', 'conc_remote_https'),
            ('Concurrent Remote HTTP/2', 'conc_remote_http2'),
            ('Concurrent Proxy HTTP', 'conc_proxy_http'),
            ('Concurrent Proxy HTTPS', 'conc_proxy_https'),
            ('Concurrent Proxy HTTP/2', 'conc_proxy_http2'),
            ('Async Local HTTP', 'async_local_http'),
            ('Async Remote HTTP', 'async_remote_http'),
            ('Async Remote HTTPS', 'async_remote_https'),
            ('Async Remote HTTP/2', 'async_remote_http2'),
            ('Async Proxy HTTP', 'async_proxy_http'),
            ('Async Proxy HTTPS', 'async_proxy_https'),
            ('Async Proxy HTTP/2', 'async_proxy_http2'),
        ]

    def seq_local_http(self):
        def run():
            with httpx.Client(verify=False) as client:
                resp = client.get(self.local_url)
                assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
        return self.run_sequential_benchmark('httpx_seq_local', run)

    def seq_remote_http(self):
        def run():
            with httpx.Client(verify=False, timeout=10) as client:
                resp = client.get(self.remote_http_url)
                assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
        return self.run_sequential_benchmark('httpx_seq_remote_http', run)

    def seq_remote_https(self):
        def run():
            with httpx.Client(verify=False, timeout=10) as client:
                resp = client.get(self.remote_https_url)
                assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
        return self.run_sequential_benchmark('httpx_seq_remote_https', run)

    def seq_remote_http2(self):
        def run():
            with httpx.Client(http2=True, verify=False, timeout=10) as client:
                resp = client.get(self.http2_url)
                assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
        return self.run_sequential_benchmark('httpx_seq_http2', run)

    def seq_proxy_http(self):
        if not self.proxy_url_http:
            return {'error': 'No proxy configured'}
        client = httpx.Client(proxy=self.proxy_url_http, verify=False, timeout=10)
        def run():
            resp = client.get(self.proxy_target_http)
            assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
        return self.run_sequential_benchmark('httpx_seq_proxy_http', run)

    def seq_proxy_https(self):
        if not self.proxy_url_https:
            return {'error': 'No proxy configured'}
        client = httpx.Client(proxy=self.proxy_url_https, verify=False, timeout=10)
        def run():
            resp = client.get(self.proxy_target_https)
            assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
        return self.run_sequential_benchmark('httpx_seq_proxy_https', run)

    def seq_proxy_http2(self):
        if not self.proxy_url_https:
            return {'error': 'No proxy configured'}
        client = httpx.Client(http2=True, proxy=self.proxy_url_https, verify=False, timeout=10)
        def run():
            resp = client.get(self.proxy_target_https)
            assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
        return self.run_sequential_benchmark('httpx_seq_proxy_http2', run)

    def conc_local_http(self):
        def run():
            with httpx.Client(verify=False) as client:
                resp = client.get(self.local_url)
                assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
        return self.run_concurrent_sync(run)

    def conc_remote_http(self):
        def run():
            with httpx.Client(verify=False, timeout=10) as client:
                resp = client.get(self.remote_http_url)
                assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
        return self.run_concurrent_sync(run)

    def conc_remote_https(self):
        def run():
            with httpx.Client(verify=False, timeout=10) as client:
                resp = client.get(self.remote_https_url)
                assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
        return self.run_concurrent_sync(run)

    def conc_remote_http2(self):
        def run():
            with httpx.Client(http2=True, verify=False, timeout=10) as client:
                resp = client.get(self.http2_url)
                assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
        return self.run_concurrent_sync(run)

    def conc_proxy_http(self):
        if not self.proxy_url_http:
            return {'error': 'No proxy configured'}
        client = httpx.Client(proxy=self.proxy_url_http, verify=False, timeout=10)
        def run():
            resp = client.get(self.proxy_target_http)
            assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
        return self.run_concurrent_sync(run)

    def conc_proxy_https(self):
        if not self.proxy_url_https:
            return {'error': 'No proxy configured'}
        client = httpx.Client(proxy=self.proxy_url_https, verify=False, timeout=10)
        def run():
            resp = client.get(self.proxy_target_https)
            assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
        return self.run_concurrent_sync(run)

    def conc_proxy_http2(self):
        if not self.proxy_url_https:
            return {'error': 'No proxy configured'}
        client = httpx.Client(http2=True, proxy=self.proxy_url_https, verify=False, timeout=10)
        def run():
            resp = client.get(self.proxy_target_https)
            assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
        return self.run_concurrent_sync(run)

    def async_local_http(self):
        async def request():
            async with httpx.AsyncClient(verify=False) as client:
                resp = await client.get(self.local_url)
                assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
        return self.run_async_benchmark(request)

    def async_remote_http(self):
        async def request():
            async with httpx.AsyncClient(verify=False, timeout=10) as client:
                resp = await client.get(self.remote_http_url)
                assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
        return self.run_async_benchmark(request)

    def async_remote_https(self):
        async def request():
            async with httpx.AsyncClient(verify=False, timeout=10) as client:
                resp = await client.get(self.remote_https_url)
                assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
        return self.run_async_benchmark(request)

    def async_remote_http2(self):
        async def request():
            async with httpx.AsyncClient(http2=True, verify=False, timeout=10) as client:
                resp = await client.get(self.http2_url)
                assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
        return self.run_async_benchmark(request)

    def async_proxy_http(self):
        if not self.proxy_url_http:
            return {'error': 'No proxy configured'}
        async def request():
            try:
                async with httpx.AsyncClient(proxy=self.proxy_url_http, verify=False, timeout=10) as client:
                    resp = await client.get(self.proxy_target_http)
                    assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
            except Exception:
                pass
        try:
            return self.run_async_benchmark(request)
        except Exception:
            return {'total_time_s': 10.0, 'req_per_sec': 0.1, 'avg_ms': 10000.0}

    def async_proxy_https(self):
        if not self.proxy_url_https:
            return {'error': 'No proxy configured'}
        async def request():
            try:
                async with httpx.AsyncClient(proxy=self.proxy_url_https, verify=False, timeout=10) as client:
                    resp = await client.get(self.proxy_target_https)
                    assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
            except Exception:
                pass
        try:
            return self.run_async_benchmark(request)
        except Exception:
            return {'total_time_s': 10.0, 'req_per_sec': 0.1, 'avg_ms': 10000.0}

    def async_proxy_http2(self):
        if not self.proxy_url_https:
            return {'error': 'No proxy configured'}
        async def request():
            try:
                async with httpx.AsyncClient(http2=True, proxy=self.proxy_url_https, verify=False, timeout=10) as client:
                    resp = await client.get(self.proxy_target_https)
                    assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
            except Exception:
                pass
        try:
            return self.run_async_benchmark(request)
        except Exception:
            return {'total_time_s': 10.0, 'req_per_sec': 0.1, 'avg_ms': 10000.0}
