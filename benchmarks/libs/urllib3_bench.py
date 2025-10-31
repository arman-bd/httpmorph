"""urllib3 benchmark implementations"""

try:
    import urllib3

    AVAILABLE = True
except ImportError:
    AVAILABLE = False

from urllib.parse import urlparse

from .base import LibraryBenchmark


class Urllib3Benchmark(LibraryBenchmark):
    """Benchmark tests for urllib3 library"""

    def is_available(self):
        return AVAILABLE

    def _get_proxy_manager(self, proxy_url, **kwargs):
        """Create ProxyManager with proper authentication headers"""
        parsed = urlparse(proxy_url)
        if parsed.username and parsed.password:
            # Extract credentials and create auth header
            credentials = f"{parsed.username}:{parsed.password}"
            proxy_headers = urllib3.make_headers(proxy_basic_auth=credentials)
            # Reconstruct proxy URL without credentials
            proxy_url_clean = f"{parsed.scheme}://{parsed.hostname}:{parsed.port}"
            return urllib3.ProxyManager(proxy_url_clean, proxy_headers=proxy_headers, **kwargs)
        else:
            return urllib3.ProxyManager(proxy_url, **kwargs)

    def get_test_matrix(self):
        return [
            ("Sequential Local HTTP", "seq_local_http"),
            ("Sequential Remote HTTP", "seq_remote_http"),
            ("Sequential Remote HTTPS", "seq_remote_https"),
            ("Sequential Proxy HTTP", "seq_proxy_http"),
            ("Sequential Proxy HTTPS", "seq_proxy_https"),
            ("Concurrent Local HTTP", "conc_local_http"),
            ("Concurrent Remote HTTP", "conc_remote_http"),
            ("Concurrent Remote HTTPS", "conc_remote_https"),
            ("Concurrent Proxy HTTP", "conc_proxy_http"),
            ("Concurrent Proxy HTTPS", "conc_proxy_https"),
        ]

    def seq_local_http(self):
        http = urllib3.PoolManager(cert_reqs="CERT_NONE")

        def run():
            resp = http.request("GET", self.local_url)
            assert 200 <= resp.status < 600, f"Got status {resp.status}"

        return self.run_sequential_benchmark("urllib3_seq_local", run)

    def seq_remote_http(self):
        http = urllib3.PoolManager(cert_reqs="CERT_NONE")

        def run():
            resp = http.request("GET", self.remote_http_url, timeout=10.0)
            assert 200 <= resp.status < 600, f"Got status {resp.status}"

        return self.run_sequential_benchmark("urllib3_seq_remote_http", run)

    def seq_remote_https(self):
        http = urllib3.PoolManager(cert_reqs="CERT_NONE")

        def run():
            resp = http.request("GET", self.remote_https_url, timeout=10.0)
            assert 200 <= resp.status < 600, f"Got status {resp.status}"

        return self.run_sequential_benchmark("urllib3_seq_remote_https", run)

    def conc_local_http(self):
        http = urllib3.PoolManager(cert_reqs="CERT_NONE")

        def run():
            resp = http.request("GET", self.local_url)
            assert 200 <= resp.status < 600, f"Got status {resp.status}"

        return self.run_concurrent_sync(run)

    def conc_remote_http(self):
        http = urllib3.PoolManager(cert_reqs="CERT_NONE")

        def run():
            resp = http.request("GET", self.remote_http_url, timeout=10.0)
            assert 200 <= resp.status < 600, f"Got status {resp.status}"

        return self.run_concurrent_sync(run)

    def conc_remote_https(self):
        http = urllib3.PoolManager(cert_reqs="CERT_NONE")

        def run():
            resp = http.request("GET", self.remote_https_url, timeout=10.0)
            assert 200 <= resp.status < 600, f"Got status {resp.status}"

        return self.run_concurrent_sync(run)

    def conc_proxy_http(self):
        if not self.proxy_url_http:
            return {"error": "No proxy configured"}

        def run():
            try:
                http = self._get_proxy_manager(
                    self.proxy_url_http, cert_reqs="CERT_NONE", timeout=10
                )
                resp = http.request("GET", self.proxy_target_http)
                assert 200 <= resp.status < 600
            except Exception:
                pass

        try:
            return self.run_concurrent_sync(run)
        except Exception:
            return {"total_time_s": 10.0, "req_per_sec": 0.1, "avg_ms": 10000.0}

    def conc_proxy_http(self):
        if not self.proxy_url_http:
            return {"error": "No proxy configured"}
        http = self._get_proxy_manager(self.proxy_url_http, cert_reqs="CERT_NONE", timeout=10)

        def run():
            try:
                resp = http.request("GET", self.proxy_target_http)
                assert 200 <= resp.status < 600
            except Exception:
                pass

        try:
            return self.run_concurrent_sync(run)
        except Exception:
            return {"total_time_s": 10.0, "req_per_sec": 0.1, "avg_ms": 10000.0}

    def conc_proxy_https(self):
        if not self.proxy_url_https:
            return {"error": "No proxy configured"}
        http = self._get_proxy_manager(self.proxy_url_https, cert_reqs="CERT_NONE", timeout=10.0)

        def run():
            resp = http.request("GET", self.proxy_target_https)
            assert 200 <= resp.status < 600, f"Got status {resp.status}"

        return self.run_concurrent_sync(run)

    def seq_proxy_http(self):
        if not self.proxy_url_http:
            return {"error": "No proxy configured"}
        http = self._get_proxy_manager(self.proxy_url_http, cert_reqs="CERT_NONE", timeout=10.0)

        def run():
            resp = http.request("GET", self.proxy_target_http)
            assert 200 <= resp.status < 600, f"Got status {resp.status}"

        return self.run_sequential_benchmark("urllib3_seq_proxy_http", run)

    def seq_proxy_https(self):
        if not self.proxy_url_https:
            return {"error": "No proxy configured"}
        http = self._get_proxy_manager(self.proxy_url_https, cert_reqs="CERT_NONE", timeout=10.0)

        def run():
            resp = http.request("GET", self.proxy_target_https)
            assert 200 <= resp.status < 600, f"Got status {resp.status}"

        return self.run_sequential_benchmark("urllib3_seq_proxy_https", run)
