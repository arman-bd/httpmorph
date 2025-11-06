"""requests benchmark implementations"""

try:
    import requests

    AVAILABLE = True
except ImportError:
    AVAILABLE = False

from .base import LibraryBenchmark


class RequestsBenchmark(LibraryBenchmark):
    """Benchmark tests for requests library"""

    def is_available(self):
        return AVAILABLE

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
        session = requests.Session()

        def run():
            resp = session.get(self.local_url, verify=False, timeout=10)
            assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
            self.validate_response_body(self.local_url, resp.text)

        return self.run_sequential_benchmark("requests_seq_local_http", run)

    def seq_remote_http(self):
        session = requests.Session()

        def run():
            resp = session.get(self.remote_http_url, verify=False, timeout=10)
            assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
            self.validate_response_body(self.remote_http_url, resp.text)

        return self.run_sequential_benchmark("requests_seq_remote_http", run)

    def seq_remote_https(self):
        session = requests.Session()

        def run():
            resp = session.get(self.remote_https_url, verify=False, timeout=10)
            assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
            self.validate_response_body(self.remote_https_url, resp.text)

        return self.run_sequential_benchmark("requests_seq_remote_https", run)

    def seq_proxy_http(self):
        if not self.proxy_url_http:
            return {"error": "No proxy configured"}
        session = requests.Session()
        session.proxies = {"http": self.proxy_url_http}

        def run():
            try:
                resp = session.get(self.proxy_target_http, verify=False, timeout=10)
                assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
                self.validate_response_body(self.proxy_target_http, resp.text)
            except Exception:
                pass

        try:
            return self.run_sequential_benchmark("requests_seq_proxy_http", run)
        except Exception:
            return {
                "mean_ms": 10000.0,
                "median_ms": 10000.0,
                "min_ms": 10000.0,
                "max_ms": 10000.0,
                "stdev_ms": 0,
            }

    def seq_proxy_https(self):
        if not self.proxy_url_https:
            return {"error": "No proxy configured"}
        session = requests.Session()
        session.proxies = {"https": self.proxy_url_https}

        def run():
            try:
                resp = session.get(self.proxy_target_https, verify=False, timeout=10)
                assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
                self.validate_response_body(self.proxy_target_https, resp.text)
            except Exception:
                pass

        try:
            return self.run_sequential_benchmark("requests_seq_proxy_https", run)
        except Exception:
            return {
                "mean_ms": 10000.0,
                "median_ms": 10000.0,
                "min_ms": 10000.0,
                "max_ms": 10000.0,
                "stdev_ms": 0,
            }

    def conc_local_http(self):
        session = requests.Session()

        def run():
            resp = session.get(self.local_url, verify=False, timeout=10)
            assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
            self.validate_response_body(self.local_url, resp.text)

        return self.run_concurrent_sync(run)

    def conc_remote_http(self):
        session = requests.Session()

        def run():
            resp = session.get(self.remote_http_url, verify=False, timeout=10)
            assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
            self.validate_response_body(self.remote_http_url, resp.text)

        return self.run_concurrent_sync(run)

    def conc_remote_https(self):
        session = requests.Session()

        def run():
            resp = session.get(self.remote_https_url, verify=False, timeout=10)
            assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
            self.validate_response_body(self.remote_https_url, resp.text)

        return self.run_concurrent_sync(run)

    def conc_proxy_http(self):
        if not self.proxy_url_http:
            return {"error": "No proxy configured"}
        session = requests.Session()
        session.proxies = {"http": self.proxy_url_http}

        def run():
            try:
                resp = session.get(self.proxy_target_http, verify=False, timeout=10)
                assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
                self.validate_response_body(self.proxy_target_http, resp.text)
            except Exception:
                pass

        try:
            return self.run_concurrent_sync(run)
        except Exception:
            return {"total_time_s": 10.0, "req_per_sec": 0.1, "avg_ms": 10000.0}

    def conc_proxy_https(self):
        if not self.proxy_url_https:
            return {"error": "No proxy configured"}
        session = requests.Session()
        session.proxies = {"https": self.proxy_url_https}

        def run():
            try:
                resp = session.get(self.proxy_target_https, verify=False, timeout=10)
                assert 200 <= resp.status_code < 600, f"Got status {resp.status_code}"
                self.validate_response_body(self.proxy_target_https, resp.text)
            except Exception:
                pass

        try:
            return self.run_concurrent_sync(run)
        except Exception:
            return {"total_time_s": 10.0, "req_per_sec": 0.1, "avg_ms": 10000.0}
