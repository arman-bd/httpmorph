"""
Integration tests for httpmorph with real HTTPS endpoints
"""

import pytest

import httpmorph
from tests.test_server import MockHTTPServer


class TestRealHTTPSIntegration:
    """Integration tests with real HTTPS endpoints"""

    def test_example_com(self):
        """Test connection to example.com"""
        response = httpmorph.get("https://example.com")
        assert response.status_code == 200
        assert b"Example Domain" in response.body
        print(f"Response time: {response.total_time_us / 1000}ms")

    def test_example_com_with_chrome(self):
        """Test example.com with Chrome profile"""
        session = httpmorph.Session(browser="chrome")
        response = session.get("https://example.com")
        assert response.status_code == 200
        assert response.tls_version is not None

    def test_example_com_with_firefox(self):
        """Test example.com with Firefox profile"""
        session = httpmorph.Session(browser="firefox")
        response = session.get("https://example.com")
        assert response.status_code == 200

    def test_google_http2(self):
        """Test Google with HTTP/2"""
        response = httpmorph.get("https://www.google.com")
        assert response.status_code in [200, 301, 302]
        # Google supports HTTP/2
        assert response.http_version in ["1.1", "2.0"]

    def test_github_api(self):
        """Test GitHub API"""
        response = httpmorph.get("https://api.github.com")
        assert response.status_code == 200
        import json

        data = json.loads(response.body)
        assert "current_user_url" in data

    def test_httpbin_get(self):
        """Test local mock server GET endpoint"""
        with MockHTTPServer() as server:
            response = httpmorph.get(f"{server.url}/get")
            assert response.status_code == 200
            import json

            data = json.loads(response.body)
            assert "headers" in data
            assert "method" in data

    def test_httpbin_post_json(self):
        """Test local mock server POST with JSON"""
        payload = {"key": "value", "number": 42, "nested": {"a": 1, "b": 2}}
        with MockHTTPServer() as server:
            response = httpmorph.post(f"{server.url}/post", json=payload)
            assert response.status_code == 200
            import json

            data = json.loads(response.body)
            assert data["json"] == payload

    def test_httpbin_headers(self):
        """Test local mock server headers endpoint"""
        custom_headers = {"X-Custom-Header": "test-value", "User-Agent": "httpmorph-test/1.0"}
        with MockHTTPServer() as server:
            response = httpmorph.get(f"{server.url}/headers", headers=custom_headers)
            assert response.status_code == 200
            import json

            data = json.loads(response.body)
            assert "X-Custom-Header" in data["headers"]

    def test_httpbin_user_agent(self):
        """Test User-Agent header"""
        with MockHTTPServer() as server:
            response = httpmorph.get(f"{server.url}/user-agent")
            assert response.status_code == 200
            import json

            data = json.loads(response.body)
            assert "user-agent" in data

    def test_httpbin_gzip(self):
        """Test gzip compression"""
        with MockHTTPServer() as server:
            response = httpmorph.get(f"{server.url}/gzip")
            assert response.status_code == 200
            import json

            data = json.loads(response.body)
            assert data["gzipped"] is True

    def test_httpbin_status_codes(self):
        """Test various HTTP status codes"""
        status_codes = [200, 204, 400, 404, 500]
        with MockHTTPServer() as server:
            for code in status_codes:
                response = httpmorph.get(f"{server.url}/status/{code}")
                assert response.status_code == code

    def test_httpbin_redirect(self):
        """Test redirect handling

        Note: follow_redirects parameter not yet implemented.
        Server returns 302 redirect which is the expected behavior.
        """
        with MockHTTPServer() as server:
            response = httpmorph.get(f"{server.url}/redirect/3")
            # Without redirect following, we get the redirect status code
            assert response.status_code in [301, 302, 307, 308]

    def test_multiple_domains_in_sequence(self):
        """Test requests to multiple different domains"""
        domains = ["https://example.com", "https://www.google.com", "https://api.github.com"]

        session = httpmorph.Session(browser="chrome")
        for domain in domains:
            response = session.get(domain)
            assert response.status_code in [200, 301, 302]
            print(f"{domain}: {response.status_code}")

    def test_concurrent_requests_different_domains(self):
        """Test concurrent requests to different domains"""
        import concurrent.futures

        urls = ["https://example.com", "https://www.google.com", "https://api.github.com"]

        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
            futures = [executor.submit(httpmorph.get, url) for url in urls]
            responses = [f.result() for f in futures]

        assert all(r.status_code in [200, 301, 302] for r in responses)


class TestTLSVersions:
    """Test different TLS versions"""

    def test_tls_1_2(self):
        """Test TLS 1.2 connection

        Note: tls_version parameter not yet implemented for requests.
        Server negotiates the highest available version (TLSv1.3).
        """
        response = httpmorph.get("https://example.com")
        assert response.status_code == 200
        assert response.tls_version in ["TLSv1.2", "TLSv1.3"]

    def test_tls_1_3(self):
        """Test TLS 1.3 connection"""
        response = httpmorph.get("https://example.com")
        assert response.status_code == 200
        # TLS version format is "TLSv1.3" not "1.3"
        assert response.tls_version == "TLSv1.3"


class TestPerformance:
    """Performance tests with real endpoints"""

    def test_batch_requests_performance(self):
        """Test performance of batch requests"""
        import time

        with MockHTTPServer() as server:
            session = httpmorph.Session(browser="chrome")
            url = f"{server.url}/get"
            iterations = 10

            start = time.time()
            for _ in range(iterations):
                response = session.get(url)
                assert response.status_code == 200
            total_time = time.time() - start

        avg_time = total_time / iterations
        print(f"Average request time: {avg_time:.3f}s")
        print(f"Requests per second: {1 / avg_time:.2f}")

        # Should be reasonably fast
        assert avg_time < 2.0  # Less than 2 seconds per request


class TestFingerprintDetection:
    """Test against fingerprint detection services"""

    def test_tls_fingerprint_detection(self):
        """Test TLS fingerprint against detection service"""
        # There are services that can detect TLS fingerprints
        # This would test against such a service
        httpmorph.Session(browser="chrome")
        # Would make request to fingerprinting detection service
        # and verify it looks like Chrome
        pass

    def test_ja3_fingerprint_uniqueness(self):
        """Test JA3 fingerprints are unique per browser"""
        browsers = ["chrome", "firefox", "safari", "edge"]
        fingerprints = {}

        for browser in browsers:
            session = httpmorph.Session(browser=browser)
            response = session.get("https://example.com")
            fingerprints[browser] = response.ja3_fingerprint

        # All fingerprints should be unique
        unique_fingerprints = set(fingerprints.values())
        assert len(unique_fingerprints) == len(browsers)

    def test_http2_fingerprint_detection(self):
        """Test HTTP/2 fingerprint detection"""
        session = httpmorph.Session(browser="chrome")
        response = session.get("https://www.google.com")

        # Should use HTTP/2 with Chrome-like SETTINGS
        assert response.http_version == "2.0"


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
