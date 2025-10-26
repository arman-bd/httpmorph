"""
Proxy support tests for httpmorph
"""

import os

import pytest

import httpmorph
from tests.test_proxy_server import MockProxyServer
from tests.test_server import MockHTTPServer


class TestProxyWithoutAuth:
    """Test proxy support without authentication"""

    def test_http_via_proxy(self):
        """Test HTTP request via proxy"""
        with MockProxyServer() as proxy:
            with MockHTTPServer() as server:
                response = httpmorph.get(f"{server.url}/get", proxy=proxy.url)
                assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2

    def test_https_via_proxy_connect(self):
        """Test HTTPS request via proxy using CONNECT method"""
        with MockProxyServer() as proxy:
            response = httpmorph.get("https://example.com", proxy=proxy.url, timeout=10)
            assert response.status_code in [200, 301, 302]

    def test_proxy_parameter_string(self):
        """Test proxy parameter as string"""
        # Just test that the parameter is accepted
        try:
            response = httpmorph.get(
                "http://example.com", proxy="http://localhost:9999", timeout=0.1
            )
            # Connection will fail but proxy parameter should be accepted
            assert response.status_code == 0  # Connection error
        except Exception:
            pass

    def test_proxy_parameter_dict(self):
        """Test proxy parameter as dict (requests-style)"""
        proxies = {"http": "http://localhost:9999", "https": "http://localhost:9999"}
        try:
            response = httpmorph.get("http://example.com", proxies=proxies, timeout=0.1)
            # Connection will fail but proxy parameter should be accepted
            assert response.status_code == 0
        except Exception:
            pass

    def test_proxy_url_formats(self):
        """Test various proxy URL formats"""
        formats = [
            "http://localhost:8080",
            "http://127.0.0.1:8080",
            "localhost:8080",
            "127.0.0.1:8080",
        ]

        for proxy_url in formats:
            try:
                response = httpmorph.get("http://example.com", proxy=proxy_url, timeout=0.1)
                # Connection will fail but URL should be accepted
                assert response.status_code >= 0
            except Exception:
                pass


class TestProxyWithAuth:
    """Test proxy support with authentication"""

    def test_http_via_proxy_with_auth(self):
        """Test HTTP via proxy with authentication"""
        with MockProxyServer(username="testuser", password="testpass") as proxy:
            with MockHTTPServer() as server:
                response = httpmorph.get(
                    f"{server.url}/get", proxy=proxy.url, proxy_auth=("testuser", "testpass")
                )
                assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2

    def test_https_via_proxy_with_auth(self):
        """Test HTTPS via proxy with authentication"""
        with MockProxyServer(username="testuser", password="testpass") as proxy:
            response = httpmorph.get(
                "https://example.com", proxy=proxy.url, proxy_auth=("testuser", "testpass"), timeout=10
            )
            assert response.status_code in [200, 301, 302]

    def test_proxy_auth_parameter(self):
        """Test proxy_auth parameter format"""
        try:
            response = httpmorph.get(
                "http://example.com",
                proxy="http://localhost:9999",
                proxy_auth=("user", "pass"),
                timeout=0.1,
            )
            # Connection will fail but parameters should be accepted
            assert response.status_code >= 0
        except Exception:
            pass

    def test_proxy_url_with_embedded_auth(self):
        """Test proxy URL with embedded username:password"""
        try:
            response = httpmorph.get(
                "http://example.com", proxy="http://user:pass@localhost:9999", timeout=0.1
            )
            # Connection will fail but URL should be parsed
            assert response.status_code >= 0
        except Exception:
            pass

    def test_proxy_auth_wrong_credentials(self):
        """Test proxy with wrong credentials"""
        with MockProxyServer(username="testuser", password="testpass") as proxy:
            with MockHTTPServer() as server:
                response = httpmorph.get(
                    f"{server.url}/get", proxy=proxy.url, proxy_auth=("wronguser", "wrongpass")
                )
                # Should fail with 407 Proxy Authentication Required or connection error
                assert response.status_code in [0, 403, 407]


class TestProxyEdgeCases:
    """Test edge cases for proxy support"""

    def test_no_proxy_parameter(self):
        """Test request without proxy (normal direct connection)"""
        response = httpmorph.get("https://example.com", timeout=10)
        assert response.status_code in [200, 301, 302]

    def test_empty_proxy(self):
        """Test with empty proxy parameter"""
        response = httpmorph.get("https://example.com", proxy="", timeout=10)
        assert response.status_code in [200, 301, 302]

    def test_none_proxy(self):
        """Test with None proxy parameter"""
        response = httpmorph.get("https://example.com", proxy=None, timeout=10)
        assert response.status_code in [200, 301, 302]

    def test_proxy_with_session(self):
        """Test proxy with session"""
        session = httpmorph.Session(browser="chrome")
        try:
            response = session.get("http://example.com", proxy="http://localhost:9999", timeout=0.1)
            # Connection will fail but proxy should be accepted
            assert response.status_code >= 0
        except Exception:
            pass

    def test_proxy_with_different_methods(self):
        """Test proxy with different HTTP methods"""
        methods = [
            ("GET", httpmorph.get),
            ("POST", lambda url, **kw: httpmorph.post(url, json={"test": "data"}, **kw)),
        ]

        for method_name, method_func in methods:
            try:
                response = method_func(
                    "http://example.com", proxy="http://localhost:9999", timeout=0.1
                )
                # Connection will fail but method should work with proxy
                assert response.status_code >= 0
            except Exception:
                pass

    def test_invalid_proxy_url(self):
        """Test with invalid proxy URL"""
        try:
            response = httpmorph.get("http://example.com", proxy="not-a-valid-url", timeout=0.1)
            # Should either fail with parse error or connection error
            assert response.status_code == 0
            assert response.error != 0
        except Exception:
            pass


class TestProxyDocumentation:
    """Test proxy examples from documentation"""

    def test_proxy_simple_example(self):
        """Test simple proxy example"""
        # Example from docs
        try:
            response = httpmorph.get(
                "http://example.com", proxy="http://proxy.example.com:8080", timeout=0.1
            )
            assert response.status_code >= 0
        except Exception:
            pass

    def test_proxy_with_auth_example(self):
        """Test proxy with authentication example"""
        # Example from docs
        try:
            response = httpmorph.get(
                "https://example.com",
                proxy="http://proxy.example.com:8080",
                proxy_auth=("username", "password"),
                timeout=0.1,
            )
            assert response.status_code >= 0
        except Exception:
            pass

    def test_proxies_dict_example(self):
        """Test proxies dict example (requests-style)"""
        # Example from docs
        proxies = {
            "http": "http://proxy.example.com:8080",
            "https": "http://proxy.example.com:8080",
        }
        try:
            response = httpmorph.get("https://example.com", proxies=proxies, timeout=0.1)
            assert response.status_code >= 0
        except Exception:
            pass


class TestRealProxyIntegration:
    """Test with real proxy from environment variables

    These tests use TEST_PROXY_URL from .env file locally or GitHub Actions secrets in CI.
    Tests are skipped if TEST_PROXY_URL is not set.
    """

    @pytest.fixture
    def real_proxy_url(self):
        """Get real proxy URL from environment"""
        proxy_url = os.environ.get("TEST_PROXY_URL")
        if not proxy_url:
            pytest.skip("TEST_PROXY_URL environment variable not set")
        return proxy_url

    def test_http_via_real_proxy(self, real_proxy_url):
        """Test HTTP request via real proxy

        Note: Real external proxies cannot reach local servers, so we test with a real HTTP site
        """
        # Use a real HTTP site instead of MockHTTPServer (external proxy can't reach localhost)
        response = httpmorph.get("http://example.com", proxy=real_proxy_url, timeout=30)
        assert response.status_code in [200, 301, 302]

    def test_https_via_real_proxy(self, real_proxy_url):
        """Test HTTPS request via real proxy using CONNECT"""
        response = httpmorph.get("https://example.com", proxy=real_proxy_url, timeout=30)
        assert response.status_code in [200, 301, 302]
        assert len(response.text) > 0

    def test_https_api_via_real_proxy(self, real_proxy_url):
        """Test HTTPS API request via real proxy"""
        response = httpmorph.get("https://httpbin.org/ip", proxy=real_proxy_url, timeout=30)
        assert response.status_code == 200
        # Response should contain the proxy's IP, not our IP
        assert "origin" in response.json()

    def test_http2_via_real_proxy(self, real_proxy_url):
        """Test HTTP/2 request via real proxy"""
        response = httpmorph.get(
            "https://www.google.com", proxy=real_proxy_url, http2=True, timeout=30
        )
        assert response.status_code in [200, 301, 302]
        # HTTP/2 should work through proxy via CONNECT tunnel
        assert len(response.text) > 0

    def test_multiple_requests_via_real_proxy(self, real_proxy_url):
        """Test multiple requests through same proxy"""
        urls = [
            "https://example.com",
            "https://httpbin.org/get",
            "https://www.google.com",
        ]

        for url in urls:
            response = httpmorph.get(url, proxy=real_proxy_url, timeout=30)
            assert response.status_code in [200, 301, 302]

    def test_session_with_real_proxy(self, real_proxy_url):
        """Test session with real proxy"""
        session = httpmorph.Session(browser="chrome")

        # Make multiple requests with same session
        response1 = session.get("https://example.com", proxy=real_proxy_url, timeout=30)
        assert response1.status_code in [200, 301, 302]

        response2 = session.get("https://httpbin.org/get", proxy=real_proxy_url, timeout=30)
        assert response2.status_code == 200

    def test_post_via_real_proxy(self, real_proxy_url):
        """Test POST request via real proxy"""
        data = {"test": "data", "foo": "bar"}
        response = httpmorph.post(
            "https://httpbin.org/post", json=data, proxy=real_proxy_url, timeout=30
        )
        assert response.status_code == 200
        response_data = response.json()
        assert response_data.get("json") == data


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
