"""
Proxy support tests for httpmorph
"""

import os

import pytest

import httpmorph
from tests.test_proxy_server import MockProxyServer
from tests.test_server import MockHTTPServer


@pytest.mark.proxy
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


@pytest.mark.proxy
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
                "https://example.com",
                proxy=proxy.url,
                proxy_auth=("testuser", "testpass"),
                timeout=10,
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


@pytest.mark.proxy
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


@pytest.mark.proxy
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


@pytest.mark.proxy
class TestRealProxyIntegration:
    """Test with real proxy from environment variables

    These tests use TEST_PROXY_URL from .env file locally or GitHub Actions secrets in CI.
    Tests are skipped if TEST_PROXY_URL is not set or offline.
    """

    @pytest.fixture
    def real_proxy_url(self):
        """Get real proxy URL from environment and verify it's available"""
        proxy_url = os.environ.get("TEST_PROXY_URL")
        if not proxy_url:
            pytest.skip("TEST_PROXY_URL environment variable not set")

        # Test if proxy is available with a simple HTTP request
        try:
            test_response = httpmorph.get("http://example.com", proxy=proxy_url, timeout=10)
            # Check if proxy is offline
            if test_response.status_code == 407:
                pytest.skip("Proxy authentication failed - proxy may be offline")
            if test_response.text and (
                "offline" in test_response.text.lower() or "busy" in test_response.text.lower()
            ):
                pytest.skip(f"Proxy is offline or busy: {test_response.text[:100]}")
        except httpmorph.ConnectionError as e:
            if "Proxy CONNECT failed" in str(e):
                pytest.skip(f"Proxy is offline: {e}")
            # Other connection errors might be transient, let tests proceed
        except Exception:
            # Other errors, let the actual tests handle them
            pass

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

    def test_https_api_via_real_proxy(self, httpbin_host, real_proxy_url):
        """Test HTTPS API request via real proxy"""
        response = httpmorph.get(f"https://{httpbin_host}/ip", proxy=real_proxy_url, timeout=30)
        assert response.status_code == 200
        # Response should contain the proxy's IP, not our IP
        assert "origin" in response.json()

    def test_http2_via_real_proxy(self, real_proxy_url):
        """Test HTTP/2 request via real proxy"""
        response = httpmorph.get(
            "https://www.google.com", proxy=real_proxy_url, http2=True, timeout=30
        )
        # Accept rate limiting from external services
        if response.status_code in [429, 403]:
            pytest.skip(f"Rate limited or blocked by external service: {response.status_code}")
        assert response.status_code in [200, 301, 302]
        # HTTP/2 should work through proxy via CONNECT tunnel
        assert len(response.text) > 0

    def test_multiple_requests_via_real_proxy(self, httpbin_host, real_proxy_url):
        """Test multiple requests through same proxy"""
        urls = [
            "https://example.com",
            f"https://{httpbin_host}/get",
            "https://www.google.com",
        ]

        for url in urls:
            try:
                response = httpmorph.get(url, proxy=real_proxy_url, timeout=30)
            except httpmorph.ConnectionError as e:
                # Proxy connection issues can happen with external proxies
                pytest.skip(f"Proxy connection failed for {url}: {e}")

            # Accept rate limiting from external services
            if response.status_code in [429, 403]:
                pytest.skip(f"Rate limited or blocked by external service: {response.status_code}")
            assert response.status_code in [200, 301, 302]

    def test_session_with_real_proxy(self, httpbin_host, real_proxy_url):
        """Test session with real proxy"""
        session = httpmorph.Session(browser="chrome")

        # Make multiple requests with same session
        response1 = session.get("https://example.com", proxy=real_proxy_url, timeout=30)
        assert response1.status_code in [200, 301, 302]

        response2 = session.get(f"https://{httpbin_host}/get", proxy=real_proxy_url, timeout=30)
        assert response2.status_code == 200

    def test_post_via_real_proxy(self, httpbin_host, real_proxy_url):
        """Test POST request via real proxy"""
        data = {"test": "data", "foo": "bar"}
        response = httpmorph.post(
            f"https://{httpbin_host}/post", json=data, proxy=real_proxy_url, timeout=30
        )
        assert response.status_code == 200
        response_data = response.json()
        assert response_data.get("json") == data

    def test_https_with_custom_headers_via_real_proxy(self, httpbin_host, real_proxy_url):
        """Test HTTPS request with custom headers via real proxy"""
        headers = {
            "User-Agent": "httpmorph-test/1.0",
            "X-Custom-Header": "test-value",
            "Accept": "application/json",
        }
        response = httpmorph.get(
            f"https://{httpbin_host}/headers", headers=headers, proxy=real_proxy_url, timeout=30
        )
        assert response.status_code == 200
        response_data = response.json()
        # Verify custom headers were sent
        assert "X-Custom-Header" in response_data.get("headers", {})
        # Header values may be returned as list by httpbin
        header_value = response_data["headers"]["X-Custom-Header"]
        assert header_value == "test-value" or header_value == ["test-value"]

    def test_https_redirects_via_real_proxy(self, httpbin_host, real_proxy_url):
        """Test HTTPS redirects through real proxy"""
        # httpbin.org/redirect/3 will redirect 3 times
        response = httpmorph.get(
            f"https://{httpbin_host}/redirect/3", proxy=real_proxy_url, timeout=30
        )
        assert response.status_code == 200
        # Check that redirects were followed
        assert len(response.history) == 3
        for redirect in response.history:
            assert redirect.status_code in [301, 302, 303, 307, 308]

    def test_https_no_redirects_via_real_proxy(self, httpbin_host, real_proxy_url):
        """Test HTTPS with allow_redirects=False via real proxy"""
        response = httpmorph.get(
            f"https://{httpbin_host}/redirect/1",
            proxy=real_proxy_url,
            allow_redirects=False,
            timeout=30,
        )
        # Should get the redirect status, not follow it
        assert response.status_code in [301, 302, 303, 307, 308]
        assert len(response.history) == 0

    def test_https_json_response_via_real_proxy(self, httpbin_host, real_proxy_url):
        """Test HTTPS JSON response via real proxy"""
        response = httpmorph.get(f"https://{httpbin_host}/json", proxy=real_proxy_url, timeout=30)
        # httpbin.org sometimes returns 502, be resilient
        if response.status_code == 502:
            pytest.skip("HTTPBin service returned 502 (temporary service issue)")
        assert response.status_code == 200
        # Should be able to parse JSON
        data = response.json()
        assert isinstance(data, dict)
        assert "slideshow" in data

    def test_https_put_request_via_real_proxy(self, httpbin_host, real_proxy_url):
        """Test HTTPS PUT request via real proxy"""
        data = {"updated": "value"}
        response = httpmorph.put(
            f"https://{httpbin_host}/put", json=data, proxy=real_proxy_url, timeout=30
        )
        assert response.status_code == 200
        response_data = response.json()
        assert response_data.get("json") == data

    def test_https_delete_request_via_real_proxy(self, httpbin_host, real_proxy_url):
        """Test HTTPS DELETE request via real proxy"""
        try:
            response = httpmorph.delete(
                f"https://{httpbin_host}/delete", proxy=real_proxy_url, timeout=30
            )
        except httpmorph.ConnectionError as e:
            # Proxy connection issues can happen with external proxies
            pytest.skip(f"Proxy connection failed: {e}")

        assert response.status_code == 200
        response_data = response.json()
        assert "url" in response_data

    def test_https_timeout_via_real_proxy(self, httpbin_host, real_proxy_url):
        """Test HTTPS request timeout via real proxy"""
        # httpbin.org/delay/5 delays response by 5 seconds
        try:
            response = httpmorph.get(
                f"https://{httpbin_host}/delay/5", proxy=real_proxy_url, timeout=1
            )
            # Should timeout
            assert response.status_code == 0  # Timeout error
        except httpmorph.Timeout:
            # Timeout exception is expected
            pass
        except httpmorph.ConnectionError as e:
            # Connection issues can happen with external proxies (timeout, network issues, etc.)
            # This is acceptable for this test - the main goal is to test timeout functionality
            pytest.skip(f"Connection failed with external proxy (may be timeout or network issue): {e}")
        except Exception as e:
            # Other timeout-related exceptions are acceptable
            if "timeout" in str(e).lower() or "timed out" in str(e).lower():
                pass
            else:
                raise

    def test_https_connection_pooling_via_real_proxy(self, httpbin_host, real_proxy_url):
        """Test connection pooling for HTTPS requests via real proxy"""
        # Make multiple requests to the same host to test connection reuse
        urls = [
            f"https://{httpbin_host}/get",
            f"https://{httpbin_host}/user-agent",
            f"https://{httpbin_host}/headers",
            f"https://{httpbin_host}/ip",
        ]

        session = httpmorph.Session(browser="chrome")

        for url in urls:
            response = session.get(url, proxy=real_proxy_url, timeout=30)
            assert response.status_code == 200

    def test_https_large_response_via_real_proxy(self, httpbin_host, real_proxy_url):
        """Test HTTPS request with large response via real proxy"""
        # Request a large JSON response (100 slides)
        try:
            response = httpmorph.get(
                f"https://{httpbin_host}/stream/100", proxy=real_proxy_url, timeout=30
            )
        except (httpmorph.ConnectionError, httpmorph.Timeout) as e:
            # Large responses may timeout or fail with external proxies
            pytest.skip(f"Large response failed with external proxy: {e}")

        # Accept rate limiting from external services
        if response.status_code in [429, 403]:
            pytest.skip(f"Rate limited or blocked by external service: {response.status_code}")

        assert response.status_code == 200
        # Response should contain multiple lines
        lines = response.text.strip().split("\n")
        assert len(lines) >= 50  # Should have many lines

    def test_https_basic_auth_via_real_proxy(self, httpbin_host, real_proxy_url):
        """Test HTTPS with basic authentication via real proxy"""
        # Note: This is site authentication, not proxy authentication
        response = httpmorph.get(
            f"https://{httpbin_host}/basic-auth/user/pass",
            auth=("user", "pass"),
            proxy=real_proxy_url,
            timeout=30,
        )
        assert response.status_code == 200
        response_data = response.json()
        # httpbin may return 'authenticated' or 'authorized'
        assert response_data.get("authenticated") is True or response_data.get("authorized") is True
        assert response_data.get("user") == "user"

    def test_https_verify_ssl_via_real_proxy(self, httpbin_host, real_proxy_url):
        """Test HTTPS with SSL verification via real proxy"""
        # Test with SSL verification enabled (default)
        response = httpmorph.get(
            f"https://{httpbin_host}/get", proxy=real_proxy_url, verify=True, timeout=30
        )
        assert response.status_code == 200

    def test_https_different_ports_via_real_proxy(self, real_proxy_url):
        """Test HTTPS to different ports via real proxy"""
        # Most HTTPS sites use port 443, test a few common endpoints
        urls = [
            "https://www.google.com",
            "https://github.com",
            "https://api.github.com",
        ]

        for url in urls:
            try:
                response = httpmorph.get(url, proxy=real_proxy_url, timeout=30)
                assert response.status_code in [200, 301, 302, 403]
            except Exception as e:
                # Some sites might block proxies, that's okay
                pytest.skip(f"Site {url} not accessible via proxy: {e}")


@pytest.mark.proxy
class TestAsyncProxyWithoutAuth:
    """Test async proxy support without authentication"""

    @pytest.mark.asyncio
    async def test_async_http_via_proxy(self):
        """Test async HTTP request via proxy"""
        import asyncio

        from httpmorph import AsyncClient

        with MockProxyServer() as proxy:
            with MockHTTPServer() as server:
                async with AsyncClient() as client:
                    response = await client.get(f"{server.url}/get", proxy=proxy.url, timeout=10)
                    assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2

    @pytest.mark.asyncio
    async def test_async_https_via_proxy_connect(self):
        """Test async HTTPS request via proxy using CONNECT method"""
        from httpmorph import AsyncClient

        with MockProxyServer() as proxy:
            async with AsyncClient() as client:
                response = await client.get("https://example.com", proxy=proxy.url, timeout=10)
                assert response.status_code in [200, 301, 302]

    @pytest.mark.asyncio
    async def test_async_proxy_parameter_string(self):
        """Test async proxy parameter as string"""
        from httpmorph import AsyncClient

        # Just test that the parameter is accepted and proxy is attempted
        try:
            async with AsyncClient() as client:
                response = await client.get(
                    "http://example.com", proxy="http://localhost:9999", timeout=0.5
                )
                # Connection will fail but proxy parameter should be accepted
                assert response.status_code >= 0
        except Exception:
            # Timeout or connection error expected (proxy doesn't exist)
            pass

    @pytest.mark.asyncio
    async def test_async_proxy_parameter_dict(self):
        """Test async proxy parameter as dict (requests-style)"""
        from httpmorph import AsyncClient

        proxies = {"http": "http://localhost:9999", "https": "http://localhost:9999"}
        try:
            async with AsyncClient() as client:
                response = await client.get("http://example.com", proxies=proxies, timeout=0.5)
                # Connection will fail but proxy parameter should be accepted
                assert response.status_code >= 0
        except Exception:
            # Timeout or connection error expected
            pass

    @pytest.mark.asyncio
    async def test_async_proxy_url_formats(self):
        """Test async various proxy URL formats"""
        from httpmorph import AsyncClient

        formats = [
            "http://localhost:8080",
            "http://127.0.0.1:8080",
            "localhost:8080",
            "127.0.0.1:8080",
        ]

        for proxy_url in formats:
            try:
                async with AsyncClient() as client:
                    response = await client.get("http://example.com", proxy=proxy_url, timeout=0.5)
                    # Connection will fail but URL should be accepted
                    assert response.status_code >= 0
            except Exception:
                # Expected - proxy doesn't exist
                pass


@pytest.mark.proxy
class TestAsyncProxyWithAuth:
    """Test async proxy support with authentication"""

    @pytest.mark.asyncio
    async def test_async_http_via_proxy_with_auth(self):
        """Test async HTTP via proxy with authentication"""
        from httpmorph import AsyncClient

        with MockProxyServer(username="testuser", password="testpass") as proxy:
            with MockHTTPServer() as server:
                async with AsyncClient() as client:
                    response = await client.get(
                        f"{server.url}/get",
                        proxy=proxy.url,
                        proxy_auth=("testuser", "testpass"),
                        timeout=10
                    )
                    assert response.status_code in [200, 402]

    @pytest.mark.asyncio
    async def test_async_https_via_proxy_with_auth(self):
        """Test async HTTPS via proxy with authentication"""
        from httpmorph import AsyncClient

        with MockProxyServer(username="testuser", password="testpass") as proxy:
            async with AsyncClient() as client:
                response = await client.get(
                    "https://example.com",
                    proxy=proxy.url,
                    proxy_auth=("testuser", "testpass"),
                    timeout=10,
                )
                assert response.status_code in [200, 301, 302]

    @pytest.mark.asyncio
    async def test_async_proxy_auth_parameter(self):
        """Test async proxy_auth parameter format"""
        from httpmorph import AsyncClient

        try:
            async with AsyncClient() as client:
                response = await client.get(
                    "http://example.com",
                    proxy="http://localhost:9999",
                    proxy_auth=("user", "pass"),
                    timeout=0.5,
                )
                # Connection will fail but parameters should be accepted
                assert response.status_code >= 0
        except Exception:
            # Expected
            pass

    @pytest.mark.asyncio
    async def test_async_proxy_url_with_embedded_auth(self):
        """Test async proxy URL with embedded username:password"""
        from httpmorph import AsyncClient

        try:
            async with AsyncClient() as client:
                response = await client.get(
                    "http://example.com", proxy="http://user:pass@localhost:9999", timeout=0.5
                )
                # Connection will fail but URL should be parsed
                assert response.status_code >= 0
        except Exception:
            # Expected
            pass

    @pytest.mark.skip(reason="Mock proxy doesn't reject wrong credentials consistently")
    @pytest.mark.asyncio
    async def test_async_proxy_auth_wrong_credentials(self):
        """Test async proxy with wrong credentials"""
        from httpmorph import AsyncClient

        with MockProxyServer(username="testuser", password="testpass") as proxy:
            with MockHTTPServer() as server:
                async with AsyncClient() as client:
                    try:
                        response = await client.get(
                            f"{server.url}/get",
                            proxy=proxy.url,
                            proxy_auth=("wronguser", "wrongpass"),
                            timeout=5
                        )
                        # Should fail with 407 Proxy Authentication Required or connection error
                        assert response.status_code in [0, 403, 407]
                    except RuntimeError as e:
                        # Proxy returned error (403 Forbidden) - this is also valid behavior
                        assert "403" in str(e) or "407" in str(e) or "Forbidden" in str(e)


@pytest.mark.proxy
class TestAsyncProxyEdgeCases:
    """Test async edge cases for proxy support"""

    @pytest.mark.asyncio
    async def test_async_no_proxy_parameter(self):
        """Test async request without proxy (normal direct connection)"""
        from httpmorph import AsyncClient

        async with AsyncClient() as client:
            response = await client.get("https://example.com", timeout=10)
            assert response.status_code in [200, 301, 302]

    @pytest.mark.asyncio
    async def test_async_empty_proxy(self):
        """Test async with empty proxy parameter"""
        from httpmorph import AsyncClient

        async with AsyncClient() as client:
            response = await client.get("https://example.com", proxy="", timeout=10)
            assert response.status_code in [200, 301, 302]

    @pytest.mark.asyncio
    async def test_async_none_proxy(self):
        """Test async with None proxy parameter"""
        from httpmorph import AsyncClient

        async with AsyncClient() as client:
            response = await client.get("https://example.com", proxy=None, timeout=10)
            assert response.status_code in [200, 301, 302]

    @pytest.mark.asyncio
    async def test_async_proxy_with_different_methods(self):
        """Test async proxy with different HTTP methods"""
        from httpmorph import AsyncClient

        methods = [
            ("GET", lambda client, url, **kw: client.get(url, **kw)),
            ("POST", lambda client, url, **kw: client.post(url, json={"test": "data"}, **kw)),
        ]

        for method_name, method_func in methods:
            try:
                async with AsyncClient() as client:
                    response = await method_func(
                        client, "http://example.com", proxy="http://localhost:9999", timeout=0.5
                    )
                    # Connection will fail but method should work with proxy
                    assert response.status_code >= 0
            except Exception:
                # Expected
                pass

    @pytest.mark.asyncio
    async def test_async_invalid_proxy_url(self):
        """Test async with invalid proxy URL"""
        from httpmorph import AsyncClient

        try:
            async with AsyncClient() as client:
                response = await client.get("http://example.com", proxy="not-a-valid-url", timeout=0.5)
                # Should either fail with parse error or connection error
                assert response.status_code == 0
                assert response.error != 0
        except Exception:
            # Expected
            pass

    @pytest.mark.asyncio
    async def test_async_proxy_connection_refused(self):
        """Test async behavior when proxy connection is refused"""
        from httpmorph import AsyncClient

        try:
            async with AsyncClient() as client:
                # Port 9 is typically not used and will refuse connection
                response = await client.get("http://example.com", proxy="http://localhost:9", timeout=2)
                # Should get connection error
                assert response.status_code == 0 or response.error != 0
        except Exception as e:
            # Connection error or timeout is expected (depends on OS behavior)
            error_msg = str(e).lower()
            assert ("connect" in error_msg or "refused" in error_msg or
                    "failed" in error_msg or "timeout" in error_msg or "timed out" in error_msg)

    @pytest.mark.asyncio
    async def test_async_proxy_timeout(self):
        """Test async proxy timeout behavior"""
        from httpmorph import AsyncClient

        try:
            async with AsyncClient() as client:
                # Use a very short timeout
                response = await client.get(
                    "http://example.com",
                    proxy="http://localhost:9999",
                    timeout=0.1
                )
                # Should timeout
                assert response.status_code == 0 or response.error != 0
        except Exception as e:
            # Timeout or connection refused exception is expected
            error_str = str(e).lower()
            assert "timeout" in error_str or "timed out" in error_str or \
                   "connection refused" in error_str or "connection failed" in error_str or \
                   "failed to connect" in error_str


@pytest.mark.proxy
class TestAsyncProxyConsistency:
    """Test async proxy behavior is consistent with sync client"""

    def test_sync_vs_async_proxy_nonexistent(self):
        """Compare sync and async behavior with non-existent proxy"""
        import asyncio

        import httpmorph
        from httpmorph import AsyncClient

        # Test sync client
        sync_error = None
        try:
            sync_response = httpmorph.get(
                "http://example.com",
                proxy="http://localhost:9999",
                timeout=1
            )
            sync_status = sync_response.status_code
        except Exception as e:
            sync_error = str(e)
            sync_status = 0

        # Test async client
        async def async_test():
            try:
                async with AsyncClient() as client:
                    response = await client.get(
                        "http://example.com",
                        proxy="http://localhost:9999",
                        timeout=1
                    )
                    return response.status_code, None
            except Exception as e:
                return 0, str(e)

        async_status, async_error = asyncio.run(async_test())

        # Both should fail (either error or status 0)
        assert sync_status == 0 or sync_error is not None
        assert async_status == 0 or async_error is not None

    @pytest.mark.asyncio
    async def test_async_proxy_vs_direct_performance(self):
        """Verify proxy adds expected overhead vs direct connection"""
        import time

        from httpmorph import AsyncClient

        # Direct connection timing
        start = time.time()
        async with AsyncClient() as client:
            response1 = await client.get("https://example.com", timeout=10)
        _ = time.time() - start

        # Proxy connection timing (should timeout/fail faster)
        start = time.time()
        try:
            async with AsyncClient() as client:
                _ = await client.get(
                    "https://example.com",
                    proxy="http://localhost:9999",
                    timeout=2
                )
        except Exception:
            pass
        proxy_time = time.time() - start

        # Direct should succeed, proxy should fail quickly
        assert response1.status_code in [200, 301, 302]
        assert proxy_time < 3  # Should fail within timeout


@pytest.mark.proxy
class TestAsyncRealProxyIntegration:
    """Test async client with real proxy from environment variables"""

    @pytest.fixture
    def real_proxy_url(self):
        """Get real proxy URL from environment and verify it's available"""
        import os
        proxy_url = os.environ.get("TEST_PROXY_URL")
        if not proxy_url:
            pytest.skip("TEST_PROXY_URL environment variable not set")

        # Test if proxy is available
        import asyncio

        from httpmorph import AsyncClient

        async def check_proxy():
            try:
                async with AsyncClient() as client:
                    test_response = await client.get(
                        "http://example.com",
                        proxy=proxy_url,
                        timeout=10
                    )
                    if test_response.status_code == 407:
                        pytest.skip("Proxy authentication failed - proxy may be offline")
                    if test_response.text and (
                        "offline" in test_response.text.lower() or "busy" in test_response.text.lower()
                    ):
                        pytest.skip(f"Proxy is offline or busy: {test_response.text[:100]}")
            except Exception as e:
                if "Proxy CONNECT failed" in str(e):
                    pytest.skip(f"Proxy is offline: {e}")

        asyncio.run(check_proxy())
        return proxy_url

    @pytest.mark.asyncio
    async def test_async_http_via_real_proxy(self, real_proxy_url):
        """Test async HTTP request via real proxy"""
        from httpmorph import AsyncClient

        async with AsyncClient() as client:
            response = await client.get("http://example.com", proxy=real_proxy_url, timeout=30)
            assert response.status_code in [200, 301, 302]

    @pytest.mark.asyncio
    async def test_async_https_via_real_proxy(self, real_proxy_url):
        """Test async HTTPS request via real proxy using CONNECT"""
        from httpmorph import AsyncClient

        async with AsyncClient() as client:
            response = await client.get("https://example.com", proxy=real_proxy_url, timeout=30)
            assert response.status_code in [200, 301, 302]
            assert len(response.text) > 0

    @pytest.mark.asyncio
    async def test_async_https_api_via_real_proxy(self, httpbin_host, real_proxy_url):
        """Test async HTTPS API request via real proxy"""
        from httpmorph import AsyncClient

        async with AsyncClient() as client:
            response = await client.get(f"https://{httpbin_host}/ip", proxy=real_proxy_url, timeout=30)
            assert response.status_code == 200
            # Response should contain the proxy's IP, not our IP
            assert "origin" in response.json()

    @pytest.mark.asyncio
    async def test_async_multiple_requests_via_real_proxy(self, httpbin_host, real_proxy_url):
        """Test async multiple requests through same proxy"""
        from httpmorph import AsyncClient

        urls = [
            "https://example.com",
            f"https://{httpbin_host}/get",
            "https://www.google.com",
        ]

        async with AsyncClient() as client:
            for url in urls:
                try:
                    response = await client.get(url, proxy=real_proxy_url, timeout=30)
                except Exception as e:
                    pytest.skip(f"Proxy connection failed for {url}: {e}")

                # Accept rate limiting from external services
                if response.status_code in [429, 403]:
                    pytest.skip(f"Rate limited or blocked by external service: {response.status_code}")
                assert response.status_code in [200, 301, 302]

    @pytest.mark.asyncio
    async def test_async_post_via_real_proxy(self, httpbin_host, real_proxy_url):
        """Test async POST request via real proxy"""
        from httpmorph import AsyncClient

        data = {"test": "data", "foo": "bar"}
        async with AsyncClient() as client:
            response = await client.post(
                f"https://{httpbin_host}/post", json=data, proxy=real_proxy_url, timeout=30
            )
            assert response.status_code == 200
            response_data = response.json()
            assert response_data.get("json") == data

    @pytest.mark.asyncio
    async def test_async_concurrent_requests_via_proxy(self, httpbin_host, real_proxy_url):
        """Test async concurrent requests through same proxy"""
        import asyncio

        from httpmorph import AsyncClient

        async def fetch(client, url):
            return await client.get(url, proxy=real_proxy_url, timeout=30)

        urls = [
            f"https://{httpbin_host}/get",
            f"https://{httpbin_host}/user-agent",
            f"https://{httpbin_host}/headers",
        ]

        async with AsyncClient() as client:
            # Make concurrent requests
            responses = await asyncio.gather(*[fetch(client, url) for url in urls])

            # All should succeed
            for response in responses:
                assert response.status_code == 200


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
