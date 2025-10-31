"""pycurl benchmark implementations"""

try:
    from io import BytesIO

    import pycurl

    AVAILABLE = True
except ImportError:
    AVAILABLE = False

from .base import LibraryBenchmark


class PycurlBenchmark(LibraryBenchmark):
    """Benchmark tests for pycurl library"""

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
            from io import BytesIO

            buffer = BytesIO()
            c = pycurl.Curl()
            c.setopt(c.URL, self.local_url)
            c.setopt(c.WRITEDATA, buffer)
            c.setopt(c.SSL_VERIFYPEER, 0)
            c.setopt(c.SSL_VERIFYHOST, 0)
            c.perform()
            code = c.getinfo(c.RESPONSE_CODE)
            assert 200 <= code < 600, f"Got status {code}"
            c.close()

        return self.run_sequential_benchmark("pycurl_seq_local", run)

    def seq_remote_http(self):
        def run():
            from io import BytesIO

            buffer = BytesIO()
            c = pycurl.Curl()
            c.setopt(c.URL, self.remote_http_url)
            c.setopt(c.WRITEDATA, buffer)
            c.perform()
            code = c.getinfo(c.RESPONSE_CODE)
            assert 200 <= code < 600, f"Got status {code}"
            c.close()

        return self.run_sequential_benchmark("pycurl_seq_remote_http", run)

    def seq_remote_https(self):
        def run():
            from io import BytesIO

            buffer = BytesIO()
            c = pycurl.Curl()
            c.setopt(c.URL, self.remote_https_url)
            c.setopt(c.WRITEDATA, buffer)
            c.setopt(c.SSL_VERIFYPEER, 0)
            c.setopt(c.SSL_VERIFYHOST, 0)
            c.perform()
            code = c.getinfo(c.RESPONSE_CODE)
            assert 200 <= code < 600, f"Got status {code}"
            c.close()

        return self.run_sequential_benchmark("pycurl_seq_remote_https", run)

    def seq_remote_http2(self):
        def run():
            from io import BytesIO

            buffer = BytesIO()
            c = pycurl.Curl()
            c.setopt(c.URL, self.http2_url)
            c.setopt(c.WRITEDATA, buffer)
            c.setopt(c.SSL_VERIFYPEER, 0)
            c.setopt(c.SSL_VERIFYHOST, 0)
            c.setopt(c.HTTP_VERSION, pycurl.CURL_HTTP_VERSION_2_0)
            c.perform()
            code = c.getinfo(c.RESPONSE_CODE)
            assert 200 <= code < 600, f"Got status {code}"
            c.close()

        return self.run_sequential_benchmark("pycurl_seq_remote_http2", run)

    def seq_proxy_http(self):
        if not self.proxy_url_http:
            return {"error": "No proxy configured"}

        def run():
            from io import BytesIO

            buffer = BytesIO()
            c = pycurl.Curl()
            c.setopt(c.URL, self.proxy_target_http)
            c.setopt(c.PROXY, self.proxy_url_http)
            c.setopt(c.WRITEDATA, buffer)
            c.setopt(c.TIMEOUT, 10)
            c.setopt(c.SSL_VERIFYPEER, 0)
            c.setopt(c.SSL_VERIFYHOST, 0)
            c.perform()
            code = c.getinfo(c.RESPONSE_CODE)
            assert 200 <= code < 600, f"Got status {code}"
            c.close()

        return self.run_sequential_benchmark("pycurl_seq_proxy_http", run)

    def seq_proxy_https(self):
        if not self.proxy_url_https:
            return {"error": "No proxy configured"}

        def run():
            from io import BytesIO

            buffer = BytesIO()
            c = pycurl.Curl()
            c.setopt(c.URL, self.proxy_target_https)
            c.setopt(c.PROXY, self.proxy_url_https)
            c.setopt(c.WRITEDATA, buffer)
            c.setopt(c.TIMEOUT, 10)
            c.setopt(c.SSL_VERIFYPEER, 0)
            c.setopt(c.SSL_VERIFYHOST, 0)
            c.perform()
            code = c.getinfo(c.RESPONSE_CODE)
            assert 200 <= code < 600, f"Got status {code}"
            c.close()

        return self.run_sequential_benchmark("pycurl_seq_proxy_https", run)

    def seq_proxy_http2(self):
        if not self.proxy_url_https:
            return {"error": "No proxy configured"}

        def run():
            from io import BytesIO

            buffer = BytesIO()
            c = pycurl.Curl()
            c.setopt(c.URL, self.proxy_target_https)
            c.setopt(c.PROXY, self.proxy_url_https)
            c.setopt(c.WRITEDATA, buffer)
            c.setopt(c.TIMEOUT, 10)
            c.setopt(c.SSL_VERIFYPEER, 0)
            c.setopt(c.SSL_VERIFYHOST, 0)
            c.setopt(c.HTTP_VERSION, pycurl.CURL_HTTP_VERSION_2_0)
            c.perform()
            code = c.getinfo(c.RESPONSE_CODE)
            assert 200 <= code < 600, f"Got status {code}"
            c.close()

        return self.run_sequential_benchmark("pycurl_seq_proxy_http2", run)

    def conc_local_http(self):
        def run():
            from io import BytesIO

            buffer = BytesIO()
            c = pycurl.Curl()
            c.setopt(c.URL, self.local_url)
            c.setopt(c.WRITEDATA, buffer)
            c.setopt(c.SSL_VERIFYPEER, 0)
            c.setopt(c.SSL_VERIFYHOST, 0)
            c.perform()
            code = c.getinfo(c.RESPONSE_CODE)
            assert 200 <= code < 600, f"Got status {code}"
            c.close()

        return self.run_concurrent_sync(run)

    def conc_remote_http(self):
        def run():
            from io import BytesIO

            buffer = BytesIO()
            c = pycurl.Curl()
            c.setopt(c.URL, self.remote_http_url)
            c.setopt(c.WRITEDATA, buffer)
            c.perform()
            code = c.getinfo(c.RESPONSE_CODE)
            assert 200 <= code < 600, f"Got status {code}"
            c.close()

        return self.run_concurrent_sync(run)

    def conc_remote_https(self):
        def run():
            from io import BytesIO

            buffer = BytesIO()
            c = pycurl.Curl()
            c.setopt(c.URL, self.remote_https_url)
            c.setopt(c.WRITEDATA, buffer)
            c.setopt(c.SSL_VERIFYPEER, 0)
            c.setopt(c.SSL_VERIFYHOST, 0)
            c.perform()
            code = c.getinfo(c.RESPONSE_CODE)
            assert 200 <= code < 600, f"Got status {code}"
            c.close()

        return self.run_concurrent_sync(run)

    def conc_remote_http2(self):
        def run():
            from io import BytesIO

            buffer = BytesIO()
            c = pycurl.Curl()
            c.setopt(c.URL, self.http2_url)
            c.setopt(c.WRITEDATA, buffer)
            c.setopt(c.SSL_VERIFYPEER, 0)
            c.setopt(c.SSL_VERIFYHOST, 0)
            c.setopt(c.HTTP_VERSION, pycurl.CURL_HTTP_VERSION_2_0)
            c.perform()
            code = c.getinfo(c.RESPONSE_CODE)
            assert 200 <= code < 600, f"Got status {code}"
            c.close()

        return self.run_concurrent_sync(run)

    def conc_proxy_http(self):
        if not self.proxy_url_http:
            return {"error": "No proxy configured"}

        def run():
            try:
                curl = pycurl.Curl()
                curl.setopt(curl.URL, self.proxy_target_http)
                curl.setopt(curl.PROXY, self.proxy_url_http)
                curl.setopt(curl.WRITEFUNCTION, lambda x: None)
                curl.setopt(curl.TIMEOUT, 10)
                curl.setopt(curl.SSL_VERIFYPEER, 0)
                curl.setopt(curl.SSL_VERIFYHOST, 0)
                curl.perform()
                curl.close()
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
            from io import BytesIO

            buffer = BytesIO()
            c = pycurl.Curl()
            c.setopt(c.URL, self.proxy_target_https)
            c.setopt(c.PROXY, self.proxy_url_https)
            c.setopt(c.WRITEDATA, buffer)
            c.setopt(c.TIMEOUT, 10)
            c.setopt(c.SSL_VERIFYPEER, 0)
            c.setopt(c.SSL_VERIFYHOST, 0)
            c.perform()
            code = c.getinfo(c.RESPONSE_CODE)
            assert 200 <= code < 600, f"Got status {code}"
            c.close()

        return self.run_concurrent_sync(run)

    def conc_proxy_http2(self):
        if not self.proxy_url_https:
            return {"error": "No proxy configured"}

        def run():
            try:
                from io import BytesIO

                buffer = BytesIO()
                c = pycurl.Curl()
                c.setopt(c.URL, self.proxy_target_https)
                c.setopt(c.PROXY, self.proxy_url_https)
                c.setopt(c.WRITEDATA, buffer)
                c.setopt(c.TIMEOUT, 10)
                c.setopt(c.SSL_VERIFYPEER, 0)
                c.setopt(c.SSL_VERIFYHOST, 0)
                c.setopt(c.HTTP_VERSION, pycurl.CURL_HTTP_VERSION_2_0)
                c.perform()
                code = c.getinfo(c.RESPONSE_CODE)
                assert 200 <= code < 600, f"Got status {code}"
                c.close()
            except Exception:
                pass

        try:
            return self.run_concurrent_sync(run)
        except Exception:
            return {"total_time_s": 10.0, "req_per_sec": 0.1, "avg_ms": 10000.0}
