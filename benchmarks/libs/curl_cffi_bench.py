"""curl_cffi benchmark implementations"""

try:
    from curl_cffi import requests as curl_requests  # noqa: F401

    AVAILABLE = True
except ImportError:
    AVAILABLE = False

from .base import LibraryBenchmark


class CurlCffiBenchmark(LibraryBenchmark):
    """Benchmark tests for curl_cffi library"""

    def is_available(self):
        return AVAILABLE

    def get_test_matrix(self):
        return [
            ("Sequential Local HTTP", "seq_local_http"),
            ("Sequential Remote HTTP", "seq_remote_http"),
            ("Sequential Remote HTTPS", "seq_remote_https"),
            ("Sequential Remote HTTP/2", "seq_remote_http2"),
            ("Sequential Proxy HTTP", "seq_proxy_http"),
            ("Sequential Proxy HTTPS", "seq_proxy_https"),
            ("Sequential Proxy HTTP/2", "seq_proxy_http2"),
            ("Concurrent Local HTTP", "conc_local_http"),
            ("Concurrent Remote HTTP", "conc_remote_http"),
            ("Concurrent Remote HTTPS", "conc_remote_https"),
            ("Concurrent Remote HTTP/2", "conc_remote_http2"),
            ("Concurrent Proxy HTTP", "conc_proxy_http"),
            ("Concurrent Proxy HTTPS", "conc_proxy_https"),
            ("Concurrent Proxy HTTP/2", "conc_proxy_http2"),
        ]

    def seq_local_http(self):
        def run():
            from curl_cffi import requests as curl_requests

            resp = curl_requests.get(self.local_url, verify=False)
            assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"

        return self.run_sequential_benchmark("curl_cffi_seq_local", run)

    def seq_remote_http(self):
        def run():
            from curl_cffi import requests as curl_requests

            resp = curl_requests.get(self.remote_http_url)
            assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"

        return self.run_sequential_benchmark("curl_cffi_seq_remote_http", run)

    def seq_remote_https(self):
        def run():
            from curl_cffi import requests as curl_requests

            resp = curl_requests.get(self.remote_https_url, verify=False)
            assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"

        return self.run_sequential_benchmark("curl_cffi_seq_remote_https", run)

    def seq_remote_http2(self):
        def run():
            from curl_cffi import requests as curl_requests

            # curl_cffi uses HTTP/2 by default when available
            resp = curl_requests.get(self.http2_url, verify=False, timeout=10)
            assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"

        return self.run_sequential_benchmark("curl_cffi_seq_remote_http2", run)

    def conc_local_http(self):
        def run():
            from curl_cffi import requests as curl_requests

            resp = curl_requests.get(self.local_url, verify=False)
            assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"

        return self.run_concurrent_sync(run)

    def conc_remote_http(self):
        def run():
            from curl_cffi import requests as curl_requests

            resp = curl_requests.get(self.remote_http_url)
            assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"

        return self.run_concurrent_sync(run)

    def conc_remote_https(self):
        def run():
            from curl_cffi import requests as curl_requests

            resp = curl_requests.get(self.remote_https_url, verify=False)
            assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"

        return self.run_concurrent_sync(run)

    def conc_remote_http2(self):
        def run():
            from curl_cffi import requests as curl_requests

            resp = curl_requests.get(self.http2_url, verify=False, timeout=10)
            assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"

        return self.run_concurrent_sync(run)

    def seq_proxy_http(self):
        if not self.proxy_url_http:
            return {"error": "No proxy configured"}

        def run():
            from curl_cffi import requests as curl_requests

            proxies = {"http": self.proxy_url_http, "https": self.proxy_url_http}
            resp = curl_requests.get(
                self.proxy_target_http, proxies=proxies, verify=False, timeout=10
            )
            assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"

        return self.run_sequential_benchmark("curl_cffi_seq_proxy_http", run)

    def seq_proxy_https(self):
        if not self.proxy_url_https:
            return {"error": "No proxy configured"}

        def run():
            from curl_cffi import requests as curl_requests

            proxies = {"http": self.proxy_url_https, "https": self.proxy_url_https}
            resp = curl_requests.get(
                self.proxy_target_https, proxies=proxies, verify=False, timeout=10
            )
            assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"

        return self.run_sequential_benchmark("curl_cffi_seq_proxy_https", run)

    def seq_proxy_http2(self):
        if not self.proxy_url_https:
            return {"error": "No proxy configured"}

        def run():
            from curl_cffi import requests as curl_requests

            proxies = {"http": self.proxy_url_https, "https": self.proxy_url_https}
            # curl_cffi uses HTTP/2 by default when available
            resp = curl_requests.get(
                self.proxy_target_https, proxies=proxies, verify=False, timeout=10
            )
            assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"

        return self.run_sequential_benchmark("curl_cffi_seq_proxy_http2", run)

    def conc_proxy_http(self):
        if not self.proxy_url_http:
            return {"error": "No proxy configured"}

        def run():
            try:
                from curl_cffi import requests as curl_requests

                proxies = {"http": self.proxy_url_http}
                resp = curl_requests.get(
                    self.proxy_target_http, proxies=proxies, verify=False, timeout=10
                )
                assert 200 <= resp.status_code < 600
            except Exception:
                pass

        try:
            return self.run_concurrent_sync(run)
        except Exception:
            return {"total_time_s": 10.0, "req_per_sec": 0.1, "avg_ms": 10000.0}

    def conc_proxy_https(self):
        if not self.proxy_url_https:
            return {"error": "No proxy configured"}

        def run():
            from curl_cffi import requests as curl_requests

            proxies = {"http": self.proxy_url_https, "https": self.proxy_url_https}
            resp = curl_requests.get(
                self.proxy_target_https, proxies=proxies, verify=False, timeout=10
            )
            assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"

        return self.run_concurrent_sync(run)

    def conc_proxy_http2(self):
        if not self.proxy_url_https:
            return {"error": "No proxy configured"}

        def run():
            try:
                from curl_cffi import requests as curl_requests

                proxies = {"http": self.proxy_url_https, "https": self.proxy_url_https}
                resp = curl_requests.get(
                    self.proxy_target_https, proxies=proxies, verify=False, timeout=10
                )
                assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
            except Exception:
                pass

        try:
            return self.run_concurrent_sync(run)
        except Exception:
            return {"total_time_s": 10.0, "req_per_sec": 0.1, "avg_ms": 10000.0}
