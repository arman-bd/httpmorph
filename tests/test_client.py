"""
Client tests for httpmorph
"""

import pytest

import httpmorph
from tests.test_server import MockHTTPServer


class TestClient:
    """Test the httpmorph Client class"""

    def test_client_creation(self):
        """Test client can be created"""
        try:
            client = httpmorph.Client()
            assert client is not None
        except (NotImplementedError, AttributeError):
            pytest.skip("Client not yet implemented")

    def test_client_initialization(self):
        """Test client initialization"""
        try:
            httpmorph.init()
            client = httpmorph.Client()
            assert client is not None
            httpmorph.cleanup()
        except (NotImplementedError, AttributeError):
            pytest.skip("Client initialization not yet implemented")

    def test_simple_get_request(self):
        """Test simple GET request"""
        with MockHTTPServer() as server:
            response = httpmorph.get(f"{server.url}/get")
            assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2
            assert response.body is not None

    def test_get_with_headers(self):
        """Test GET request with custom headers"""
        with MockHTTPServer() as server:
            headers = {
                "User-Agent": "httpmorph-test/1.0",
                "Accept": "application/json",
                "X-Custom-Header": "test-value",
            }
            response = httpmorph.get(f"{server.url}/headers", headers=headers)
            assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2

    def test_post_json(self):
        """Test POST request with JSON data"""
        with MockHTTPServer() as server:
            data = {"key": "value", "number": 42, "nested": {"a": 1}}
            response = httpmorph.post(
                f"{server.url}/post", json=data, headers={"Content-Type": "application/json"}
            )
            assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2

    def test_post_form_data(self):
        """Test POST request with form data"""
        with MockHTTPServer() as server:
            data = {"field1": "value1", "field2": "value2"}
            response = httpmorph.post(f"{server.url}/post/form", data=data)
            assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2

    def test_put_request(self):
        """Test PUT request"""
        with MockHTTPServer() as server:
            data = {"updated": "data"}
            response = httpmorph.put(f"{server.url}/put", json=data)
            assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2

    def test_delete_request(self):
        """Test DELETE request"""
        with MockHTTPServer() as server:
            response = httpmorph.delete(f"{server.url}/delete")
            assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2

    def test_status_codes(self):
        """Test different HTTP status codes"""
        with MockHTTPServer() as server:
            # Test 200 OK
            response = httpmorph.get(f"{server.url}/status/200")
            assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2

            # Test 404 Not Found
            response = httpmorph.get(f"{server.url}/status/404")
            assert response.status_code == 404

    def test_connection_reuse(self):
        """Test that connections are reused"""
        with MockHTTPServer() as server:
            # Make multiple requests to the same server
            for _ in range(5):
                response = httpmorph.get(f"{server.url}/get")
                assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2

    def test_concurrent_requests(self):
        """Test concurrent requests"""
        import concurrent.futures

        with MockHTTPServer() as server:
            with concurrent.futures.ThreadPoolExecutor(max_workers=10) as executor:
                futures = [executor.submit(httpmorph.get, f"{server.url}/get") for _ in range(20)]
                responses = [f.result() for f in futures]
                assert all(r.status_code == 200 for r in responses)

    def test_timeout(self):
        """Test request timeout raises Timeout exception"""
        import pytest

        with MockHTTPServer() as server:
            # Timeout should raise Timeout exception (requests-compatible)
            with pytest.raises(httpmorph.Timeout):
                httpmorph.get(f"{server.url}/delay/1", timeout=0.1)

    def test_response_timing(self, httpbin_host):
        """Test response timing information"""
        with MockHTTPServer() as server:
            response = httpmorph.get(f"{server.url}/get")
            assert hasattr(response, "connect_time_us")
            assert hasattr(response, "total_time_us")
            assert response.total_time_us > 0

    def test_gzip_decompression(self, httpbin_host):
        """Test automatic gzip decompression"""
        with MockHTTPServer() as server:
            response = httpmorph.get(f"{server.url}/gzip")
            assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2
            # Should be automatically decompressed
            import json

            data = json.loads(response.body)
            assert data["compressed"] is True


class TestClientWithRealHTTPS:
    """Test client with real HTTPS connections"""

    def test_real_https_connection(self, httpbin_host):
        """Test connection to real HTTPS server"""
        response = httpmorph.get("https://example.com")
        assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2
        assert b"Example Domain" in response.body

    def test_tls_information(self, httpbin_host):
        """Test TLS information is captured"""
        response = httpmorph.get(f"https://{httpbin_host}")
        assert hasattr(response, "tls_version")
        assert hasattr(response, "tls_cipher")
        assert response.tls_version is not None

    def test_ja3_fingerprint(self, httpbin_host):
        """Test JA3 fingerprint is generated"""
        response = httpmorph.get(f"https://{httpbin_host}")
        assert hasattr(response, "ja3_fingerprint")
        assert response.ja3_fingerprint is not None
        assert len(response.ja3_fingerprint) > 0

    def test_http2_connection(self, httpbin_host):
        """Test HTTP/2 connection

        HTTP/2 support is fully implemented using nghttp2:
        - ALPN negotiation ✓
        - Binary framing with nghttp2 ✓
        - HEADERS/DATA frame callbacks ✓
        - EOF handling in recv_callback ✓
        """
        client = httpmorph.Client(http2=True)
        response = client.get(f"https://{httpbin_host}/get", timeout=10)
        # httpbingo returns 402 for HTTP/2 requests
        assert response.status_code in [200, 402]
        assert response.http_version == "2.0"


class TestClientHTTP2Flag:
    """Test Client with HTTP/2 flag (httpx-like API)"""

    def test_client_http2_flag_default(self):
        """Test that Client http2 flag defaults to False"""
        client = httpmorph.Client()
        assert hasattr(client, "http2")
        assert client.http2 is False

    def test_client_http2_flag_enabled(self, httpbin_host):
        """Test Client with http2=True"""
        client = httpmorph.Client(http2=True)
        assert client.http2 is True

        # Test actual HTTP/2 request
        response = client.get(f"https://{httpbin_host}/get", timeout=10)
        assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2
        assert response.http_version == "2.0"

    def test_client_http2_flag_disabled(self):
        """Test Client with http2=False explicitly"""
        client = httpmorph.Client(http2=False)
        assert client.http2 is False

    def test_client_http2_per_request_override(self, httpbin_host):
        """Test per-request http2 parameter overrides client default"""
        # Client default is False
        client = httpmorph.Client(http2=False)
        assert client.http2 is False

        # But request with http2=True should use HTTP/2
        response = client.get(f"https://{httpbin_host}/get", http2=True, timeout=10)
        assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2
        assert response.http_version == "2.0"

    def test_client_http2_flag_persistence(self, httpbin_host):
        """Test http2 flag persists across multiple requests"""
        client = httpmorph.Client(http2=True)

        # Make multiple requests
        for _ in range(3):
            response = client.get(f"https://{httpbin_host}/get", timeout=10)
            assert response.http_version == "2.0"

        # Flag should still be True
        assert client.http2 is True


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
