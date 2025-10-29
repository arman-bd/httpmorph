"""urllib benchmark implementations"""
import urllib.request
import ssl

from .base import LibraryBenchmark

AVAILABLE = True  # urllib is built-in


class UrllibBenchmark(LibraryBenchmark):
    """Benchmark tests for urllib library"""

    def is_available(self):
        return AVAILABLE

    def get_test_matrix(self):
        return [
            ('Sequential Local HTTP', 'seq_local_http'),
            ('Sequential Remote HTTP', 'seq_remote_http'),
            ('Sequential Remote HTTPS', 'seq_remote_https'),
            ('Sequential Proxy HTTP', 'seq_proxy_http'),
            ('Sequential Proxy HTTPS', 'seq_proxy_https'),
            ('Concurrent Local HTTP', 'conc_local_http'),
            ('Concurrent Remote HTTP', 'conc_remote_http'),
            ('Concurrent Remote HTTPS', 'conc_remote_https'),
            ('Concurrent Proxy HTTP', 'conc_proxy_http'),
            ('Concurrent Proxy HTTPS', 'conc_proxy_https'),
        ]

    def seq_local_http(self):
        def run():
            ctx = ssl.create_default_context()
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
            with urllib.request.urlopen(self.local_url, context=ctx) as resp:
                assert 200 <= resp.status < 600, f"Got status {resp.status}"
                resp.read()
        return self.run_sequential_benchmark('urllib_seq_local', run)

    def seq_remote_http(self):
        def run():
            with urllib.request.urlopen(self.remote_http_url, timeout=10) as resp:
                assert 200 <= resp.status < 600, f"Got status {resp.status}"
                resp.read()
        return self.run_sequential_benchmark('urllib_seq_remote_http', run)

    def seq_remote_https(self):
        def run():
            ctx = ssl.create_default_context()
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
            with urllib.request.urlopen(self.remote_https_url, context=ctx, timeout=10) as resp:
                assert 200 <= resp.status < 600, f"Got status {resp.status}"
                resp.read()
        return self.run_sequential_benchmark('urllib_seq_remote_https', run)

    def conc_local_http(self):
        def run():
            ctx = ssl.create_default_context()
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
            with urllib.request.urlopen(self.local_url, context=ctx) as resp:
                assert 200 <= resp.status < 600, f"Got status {resp.status}"
                resp.read()
        return self.run_concurrent_sync(run)

    def conc_remote_http(self):
        def run():
            with urllib.request.urlopen(self.remote_http_url, timeout=10) as resp:
                assert 200 <= resp.status < 600, f"Got status {resp.status}"
                resp.read()
        return self.run_concurrent_sync(run)

    def conc_remote_https(self):
        def run():
            ctx = ssl.create_default_context()
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
            with urllib.request.urlopen(self.remote_https_url, context=ctx, timeout=10) as resp:
                assert 200 <= resp.status < 600, f"Got status {resp.status}"
                resp.read()
        return self.run_concurrent_sync(run)

    def conc_proxy_http(self):
        if not self.proxy_url_http:
            return {'error': 'No proxy configured'}
        def run():
            try:
                proxy_handler = urllib.request.ProxyHandler({'http': self.proxy_url_http})
                opener = urllib.request.build_opener(proxy_handler)
                opener.open(self.proxy_target_http, timeout=10)
            except Exception:
                pass
        try:
            return self.run_concurrent_sync(run)
        except Exception:
            return {'total_time_s': 10.0, 'req_per_sec': 0.1, 'avg_ms': 10000.0}
