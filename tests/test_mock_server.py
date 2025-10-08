"""
Tests for the MockHTTPServer itself - these actually run!
"""

import json
import urllib.error
import urllib.request

import pytest

from tests.test_server import MockHTTPServer


class TestMockHTTPServer:
    """Test the mock HTTP server functionality"""

    def test_server_starts_and_stops(self):
        """Test server can start and stop"""
        server = MockHTTPServer()
        server.start()
        assert server.url.startswith("http://127.0.0.1:")
        server.stop()

    def test_server_context_manager(self):
        """Test server works with context manager"""
        with MockHTTPServer() as server:
            assert server.url is not None
            assert "http" in server.url

    def test_get_endpoint(self):
        """Test GET endpoint returns JSON"""
        with MockHTTPServer() as server:
            response = urllib.request.urlopen(f"{server.url}/get")
            data = json.loads(response.read())

            assert data["method"] == "GET"
            assert data["path"] == "/get"
            assert "headers" in data

    def test_post_endpoint_json(self):
        """Test POST endpoint with JSON data"""
        with MockHTTPServer() as server:
            post_data = json.dumps({"test": "value", "number": 42}).encode()
            req = urllib.request.Request(
                f"{server.url}/post",
                data=post_data,
                headers={'Content-Type': 'application/json'}
            )
            response = urllib.request.urlopen(req)
            data = json.loads(response.read())

            assert data["method"] == "POST"
            assert data["json"]["test"] == "value"
            assert data["json"]["number"] == 42

    def test_post_endpoint_form(self):
        """Test POST endpoint with form data"""
        with MockHTTPServer() as server:
            post_data = b"field1=value1&field2=value2"
            req = urllib.request.Request(
                f"{server.url}/post/form",
                data=post_data,
                headers={'Content-Type': 'application/x-www-form-urlencoded'}
            )
            response = urllib.request.urlopen(req)
            data = json.loads(response.read())

            assert data["method"] == "POST"
            assert "field1=value1" in data["form"]

    def test_status_200(self):
        """Test 200 OK status code"""
        with MockHTTPServer() as server:
            response = urllib.request.urlopen(f"{server.url}/status/200")
            assert response.status == 200

    def test_status_404(self):
        """Test 404 Not Found status code"""
        with MockHTTPServer() as server:
            with pytest.raises(urllib.error.HTTPError) as exc_info:
                urllib.request.urlopen(f"{server.url}/status/404")
            assert exc_info.value.code == 404

    def test_headers_endpoint(self):
        """Test headers endpoint returns request headers"""
        with MockHTTPServer() as server:
            req = urllib.request.Request(
                f"{server.url}/headers",
                headers={'X-Custom-Header': 'test-value'}
            )
            response = urllib.request.urlopen(req)
            data = json.loads(response.read())

            assert "headers" in data
            assert data["headers"]["X-Custom-Header"] == "test-value"

    def test_put_endpoint(self):
        """Test PUT endpoint"""
        with MockHTTPServer() as server:
            put_data = json.dumps({"updated": "data"}).encode()
            req = urllib.request.Request(
                f"{server.url}/put",
                data=put_data,
                method='PUT',
                headers={'Content-Type': 'application/json'}
            )
            response = urllib.request.urlopen(req)
            data = json.loads(response.read())

            assert data["method"] == "PUT"

    def test_delete_endpoint(self):
        """Test DELETE endpoint"""
        with MockHTTPServer() as server:
            req = urllib.request.Request(
                f"{server.url}/delete",
                method='DELETE'
            )
            response = urllib.request.urlopen(req)
            data = json.loads(response.read())

            assert data["method"] == "DELETE"

    def test_gzip_endpoint(self):
        """Test gzip compressed response"""
        with MockHTTPServer() as server:
            import gzip
            response = urllib.request.urlopen(f"{server.url}/gzip")
            compressed_data = response.read()
            decompressed_data = gzip.decompress(compressed_data)
            data = json.loads(decompressed_data)

            assert data["compressed"] is True

    def test_redirect_endpoint(self):
        """Test redirect"""
        with MockHTTPServer() as server:
            # urllib automatically follows redirects by default
            response = urllib.request.urlopen(f"{server.url}/redirect/1")
            # Should be redirected to /get
            assert response.status == 200

    def test_multiple_requests(self):
        """Test multiple requests to same server"""
        with MockHTTPServer() as server:
            for i in range(5):
                response = urllib.request.urlopen(f"{server.url}/get")
                assert response.status == 200

    def test_concurrent_servers(self):
        """Test multiple servers can run concurrently"""
        with MockHTTPServer() as server1:
            with MockHTTPServer() as server2:
                # Different ports
                assert server1.port != server2.port

                # Both work
                response1 = urllib.request.urlopen(f"{server1.url}/get")
                response2 = urllib.request.urlopen(f"{server2.url}/get")

                assert response1.status == 200
                assert response2.status == 200


class TestHTTPSServer:
    """Test HTTPS server functionality"""

    def test_https_server_starts(self):
        """Test HTTPS server can start"""
        try:
            server = MockHTTPServer(ssl_enabled=True)
            server.start()
            assert server.url.startswith("https://")
            server.stop()
        except RuntimeError as e:
            if "cryptography" in str(e):
                pytest.skip("cryptography package not available")
            raise

    def test_https_get_request(self):
        """Test HTTPS GET request"""
        try:
            import ssl
            with MockHTTPServer(ssl_enabled=True) as server:
                # Create SSL context that doesn't verify certificates
                ctx = ssl.create_default_context()
                ctx.check_hostname = False
                ctx.verify_mode = ssl.CERT_NONE

                response = urllib.request.urlopen(
                    f"{server.url}/get",
                    context=ctx
                )
                data = json.loads(response.read())
                assert data["method"] == "GET"
        except RuntimeError as e:
            if "cryptography" in str(e):
                pytest.skip("cryptography package not available")
            raise


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
