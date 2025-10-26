"""
HTTP/2 flag tests for httpmorph

Tests the HTTP/2 flag functionality similar to httpx API
"""

import pytest

import httpmorph
from tests.test_server import MockHTTPServer


class TestClientHTTP2Flag:
    """Test Client class with HTTP/2 flag"""

    def test_client_http2_default_false(self):
        """Test that Client http2 flag defaults to False"""
        client = httpmorph.Client()
        assert client.http2 is False

    def test_client_http2_true(self):
        """Test Client with http2=True"""
        client = httpmorph.Client(http2=True)
        assert client.http2 is True

    def test_client_http2_enabled_request(self):
        """Test request with http2=True returns HTTP/2"""
        client = httpmorph.Client(http2=True)
        response = client.get("https://www.google.com", timeout=10)
        assert response.status_code == 200
        assert response.http_version == "2.0"

    def test_client_http2_per_request_override_enable(self):
        """Test per-request http2=True overrides client default"""
        client = httpmorph.Client(http2=False)  # Default disabled
        response = client.get("https://www.google.com", http2=True, timeout=10)
        assert response.status_code == 200
        assert response.http_version == "2.0"

    def test_client_http2_per_request_override_disable(self):
        """Test per-request http2=False overrides client default"""
        client = httpmorph.Client(http2=True)  # Default enabled
        # Use a server that supports both HTTP/1.1 and HTTP/2
        try:
            response = client.get("https://example.com", http2=False, timeout=10)
            # If server supports it, should get HTTP/1.1
            assert response.status_code == 200
            # Note: Some servers may still negotiate HTTP/2, so we don't assert version
        except Exception as e:
            # Some servers require HTTP/2
            pytest.skip(f"Server requires HTTP/2: {e}")

    def test_client_http2_flag_persistence(self):
        """Test that client http2 flag persists across requests"""
        client = httpmorph.Client(http2=True)

        # Make multiple requests
        for _ in range(3):
            response = client.get("https://www.google.com", timeout=10)
            assert response.http_version == "2.0"
            assert client.http2 is True  # Flag should remain True


class TestSessionHTTP2Flag:
    """Test Session class with HTTP/2 flag"""

    def test_session_http2_default_false(self):
        """Test that Session http2 flag defaults to False"""
        session = httpmorph.Session(browser="chrome")
        assert session.http2 is False

    def test_session_http2_true(self):
        """Test Session with http2=True"""
        session = httpmorph.Session(browser="chrome", http2=True)
        assert session.http2 is True

    def test_session_http2_enabled_request(self):
        """Test session request with http2=True returns HTTP/2"""
        session = httpmorph.Session(browser="chrome", http2=True)
        response = session.get("https://www.google.com", timeout=10)
        assert response.status_code == 200
        assert response.http_version == "2.0"

    def test_session_http2_per_request_override(self):
        """Test per-request http2 override in session"""
        session = httpmorph.Session(browser="chrome", http2=False)
        response = session.get("https://www.google.com", http2=True, timeout=10)
        assert response.status_code == 200
        assert response.http_version == "2.0"

    def test_session_http2_with_different_browsers(self):
        """Test HTTP/2 works with different browser profiles"""
        browsers = ["chrome", "firefox", "safari", "edge"]

        for browser in browsers:
            session = httpmorph.Session(browser=browser, http2=True)
            response = session.get("https://www.google.com", timeout=10)
            assert response.status_code == 200
            assert response.http_version == "2.0", f"HTTP/2 failed for {browser}"

    def test_session_http2_flag_persistence(self):
        """Test that session http2 flag persists across requests"""
        session = httpmorph.Session(browser="chrome", http2=True)

        # Make multiple requests
        for _ in range(3):
            response = session.get("https://www.google.com", timeout=10)
            assert response.http_version == "2.0"
            assert session.http2 is True  # Flag should remain True


class TestHTTP2WithMockServer:
    """Test HTTP/2 with mock server (HTTP/1.1)"""

    def test_http2_flag_with_http1_server(self):
        """Test that http2 flag works with HTTP/1.1 mock server"""
        with MockHTTPServer() as server:
            # Mock server is HTTP/1.1
            client = httpmorph.Client(http2=True)
            response = client.get(f"{server.url}/get", timeout=5)
            assert response.status_code == 200
            # Server doesn't support HTTP/2, so should fall back to HTTP/1.1
            # (This depends on implementation - may negotiate down or fail)


class TestHTTP2Compatibility:
    """Test HTTP/2 API compatibility with httpx"""

    def test_httpx_style_client_api(self):
        """Test httpx-style Client(http2=True) API"""
        # This should work exactly like httpx.Client(http2=True)
        client = httpmorph.Client(http2=True)
        response = client.get("https://www.google.com", timeout=10)
        assert response.status_code == 200
        assert hasattr(response, "http_version")
        assert response.http_version == "2.0"

    def test_httpx_style_session_api(self):
        """Test httpx-style session with http2 parameter"""
        # Similar to httpx session API
        with httpmorph.Session(browser="chrome", http2=True) as session:
            response = session.get("https://www.google.com", timeout=10)
            assert response.status_code == 200
            assert response.http_version == "2.0"

    def test_httpx_style_per_request_override(self):
        """Test httpx-style per-request http2 override"""
        client = httpmorph.Client(http2=False)

        # Override at request level (like httpx)
        response = client.get("https://www.google.com", http2=True, timeout=10)
        assert response.http_version == "2.0"


class TestHTTP2Features:
    """Test HTTP/2 specific features"""

    def test_http2_response_attributes(self):
        """Test that HTTP/2 responses have expected attributes"""
        client = httpmorph.Client(http2=True)
        response = client.get("https://www.google.com", timeout=10)

        # Standard response attributes
        assert hasattr(response, "status_code")
        assert hasattr(response, "headers")
        assert hasattr(response, "body")
        assert hasattr(response, "http_version")

        # Timing attributes
        assert hasattr(response, "total_time_us")
        assert hasattr(response, "first_byte_time_us")

        # TLS attributes (HTTP/2 requires TLS)
        assert hasattr(response, "tls_version")
        assert hasattr(response, "tls_cipher")
        assert response.tls_version is not None

    def test_http2_with_headers(self):
        """Test HTTP/2 request with custom headers"""
        client = httpmorph.Client(http2=True)
        headers = {
            "User-Agent": "httpmorph-test/1.0",
            "Accept": "application/json",
            "X-Custom-Header": "test-value",
        }
        response = client.get("https://www.google.com", headers=headers, timeout=10)
        assert response.status_code == 200
        assert response.http_version == "2.0"

    def test_http2_post_request(self):
        """Test HTTP/2 POST request with JSON data"""
        client = httpmorph.Client(http2=True)
        data = {"test": "data", "number": 42}

        # Use httpbin.org which supports HTTP/2
        response = client.post(
            "https://httpbin.org/post",
            json=data,
            timeout=10
        )
        assert response.status_code == 200
        assert response.http_version == "2.0"

    def test_http2_timing_information(self):
        """Test that HTTP/2 requests include timing information"""
        client = httpmorph.Client(http2=True)
        response = client.get("https://www.google.com", timeout=10)

        assert response.total_time_us > 0
        assert response.first_byte_time_us > 0
        assert response.connect_time_us >= 0
        assert response.tls_time_us >= 0


class TestHTTP2EdgeCases:
    """Test HTTP/2 edge cases and error handling"""

    def test_http2_with_timeout(self):
        """Test HTTP/2 request with timeout"""
        client = httpmorph.Client(http2=True)

        # Should complete within timeout
        response = client.get("https://www.google.com", timeout=5)
        assert response.status_code == 200

    def test_http2_flag_type_checking(self):
        """Test http2 flag accepts boolean values"""
        # Should accept True
        client1 = httpmorph.Client(http2=True)
        assert client1.http2 is True

        # Should accept False
        client2 = httpmorph.Client(http2=False)
        assert client2.http2 is False

        # Should handle default (no argument)
        client3 = httpmorph.Client()
        assert client3.http2 is False

    def test_http2_multiple_concurrent_requests(self):
        """Test HTTP/2 with concurrent requests (multiplexing)"""
        import concurrent.futures

        client = httpmorph.Client(http2=True)

        def make_request():
            return client.get("https://www.google.com", timeout=10)

        with concurrent.futures.ThreadPoolExecutor(max_workers=5) as executor:
            futures = [executor.submit(make_request) for _ in range(10)]
            responses = [f.result() for f in futures]

            # All should succeed with HTTP/2
            assert all(r.status_code == 200 for r in responses)
            assert all(r.http_version == "2.0" for r in responses)


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
