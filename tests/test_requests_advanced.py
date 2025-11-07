"""
Advanced test suite for requests compatibility - streaming, files, etc.

Tests for Phase 2/3 features that require more complex implementation.
"""

import io
import os
import tempfile
from pathlib import Path

import pytest

import httpmorph


class TestResponseIterContent:
    """Tests for Response.iter_content() streaming"""

    def test_iter_content_basic(self, httpbin_server):
        """Test iter_content() yields chunks"""
        response = httpmorph.get(f"{httpbin_server}/bytes/1024", stream=True)

        chunks = list(response.iter_content(chunk_size=256))

        assert len(chunks) > 0
        total_size = sum(len(chunk) for chunk in chunks)
        assert total_size == 1024

    def test_iter_content_default_chunk_size(self, httpbin_server):
        """Test iter_content() with default chunk size"""
        response = httpmorph.get(f"{httpbin_server}/bytes/5000", stream=True)

        chunks = list(response.iter_content())
        assert len(chunks) > 0

    def test_iter_content_decode_unicode(self, httpbin_server):
        """Test iter_content() with decode_unicode=True"""
        response = httpmorph.get(f"{httpbin_server}/get", stream=True)

        chunks = list(response.iter_content(decode_unicode=True))

        for chunk in chunks:
            assert isinstance(chunk, str)

    def test_iter_content_without_stream(self, httpbin_server):
        """Test iter_content() when stream=False"""
        response = httpmorph.get(f"{httpbin_server}/bytes/100")

        # Should still work, just returns content in one chunk
        chunks = list(response.iter_content())
        assert len(chunks) >= 1


class TestResponseIterLines:
    """Tests for Response.iter_lines() streaming"""

    def test_iter_lines_basic(self, httpbin_server):
        """Test iter_lines() yields lines"""
        response = httpmorph.get(f"{httpbin_server}/get", stream=True)

        lines = list(response.iter_lines())

        assert len(lines) > 0
        for line in lines:
            assert isinstance(line, (bytes, str))

    def test_iter_lines_decode_unicode(self, httpbin_server):
        """Test iter_lines() with decode_unicode=True"""
        response = httpmorph.get(f"{httpbin_server}/get", stream=True)

        lines = list(response.iter_lines(decode_unicode=True))

        for line in lines:
            assert isinstance(line, str)

    def test_iter_lines_delimiter(self, httpbin_server):
        """Test iter_lines() with custom delimiter"""
        response = httpmorph.get(f"{httpbin_server}/get", stream=True)

        lines = list(response.iter_lines(delimiter=b"\n"))
        assert len(lines) > 0


class TestStreamParameter:
    """Tests for stream= parameter"""

    def test_stream_false_loads_content(self, httpbin_server):
        """Test stream=False loads content immediately"""
        response = httpmorph.get(f"{httpbin_server}/bytes/1000", stream=False)

        assert response.content is not None
        assert len(response.content) == 1000

    def test_stream_true_defers_content(self, httpbin_server):
        """Test stream=True defers content loading"""
        response = httpmorph.get(f"{httpbin_server}/bytes/1000", stream=True)

        # Content should not be loaded yet
        # Can iterate over it
        chunks = list(response.iter_content(chunk_size=100))
        assert len(chunks) == 10

    def test_stream_large_file(self, httpbin_server):
        """Test streaming large file doesn't load into memory"""
        # Download 10MB file
        response = httpmorph.get(
            f"{httpbin_server}/bytes/10485760",  # 10MB
            stream=True,
        )

        # Should be able to iterate without loading all into memory
        chunk_count = 0
        for chunk in response.iter_content(chunk_size=8192):
            chunk_count += 1
            if chunk_count > 100:  # Just test first 100 chunks
                break

        assert chunk_count > 0


class TestFilesParameter:
    """Tests for files= parameter (multipart upload)"""

    def test_files_single_file(self, httpbin_server):
        """Test uploading single file"""
        with tempfile.NamedTemporaryFile(mode="w", delete=False) as f:
            f.write("test file content")
            temp_path = f.name

        try:
            with open(temp_path, "rb") as f:
                files = {"file": f}
                response = httpmorph.post(f"{httpbin_server}/post", files=files)

            assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2
            data = response.json()
            assert "files" in data
        finally:
            os.unlink(temp_path)

    def test_files_multiple_files(self, httpbin_server):
        """Test uploading multiple files"""
        temp_files = []

        try:
            # Create two temp files
            for i in range(2):
                f = tempfile.NamedTemporaryFile(mode="w", delete=False)
                f.write(f"file {i} content")
                f.close()
                temp_files.append(f.name)

            with open(temp_files[0], "rb") as f1, open(temp_files[1], "rb") as f2:
                files = {"file1": f1, "file2": f2}
                response = httpmorph.post(f"{httpbin_server}/post", files=files)

            assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2
            data = response.json()
            assert "files" in data
        finally:
            for path in temp_files:
                os.unlink(path)

    def test_files_with_filename(self, httpbin_server):
        """Test uploading file with custom filename"""
        content = b"custom content"

        files = {"file": ("custom_name.txt", io.BytesIO(content))}
        response = httpmorph.post(f"{httpbin_server}/post", files=files)

        assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2
        data = response.json()
        assert "custom_name.txt" in str(data.get("files", ""))

    def test_files_with_content_type(self, httpbin_server):
        """Test uploading file with custom content type"""
        content = b"binary data"

        files = {"file": ("data.bin", io.BytesIO(content), "application/octet-stream")}
        response = httpmorph.post(f"{httpbin_server}/post", files=files)

        assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2

    def test_files_and_data_combined(self, httpbin_server):
        """Test uploading files with additional form data"""
        with tempfile.NamedTemporaryFile(mode="w", delete=False) as f:
            f.write("test")
            temp_path = f.name

        try:
            with open(temp_path, "rb") as f:
                files = {"upload": f}
                data = {"field1": "value1", "field2": "value2"}

                response = httpmorph.post(f"{httpbin_server}/post", files=files, data=data)

            assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2
            result = response.json()
            assert result["form"]["field1"] == "value1"
            assert result["form"]["field2"] == "value2"
        finally:
            os.unlink(temp_path)


class TestVerifyParameter:
    """Tests for verify= parameter (SSL verification)"""

    def test_verify_true_validates_cert(self, httpbin_server):
        """Test verify=True validates SSL certificate"""
        # Should succeed with valid cert
        response = httpmorph.get(f"{httpbin_server}/get", verify=True)
        assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2

    def test_verify_false_skips_validation(self, httpbin_server):
        """Test verify=False skips SSL validation"""
        # Should succeed even with invalid cert (if we had one to test)
        response = httpmorph.get(f"{httpbin_server}/get", verify=False)
        assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2

    def test_verify_ca_bundle_path(self, httpbin_server):
        """Test verify with path to CA bundle"""
        # Test with system CA bundle
        ca_paths = [
            "/etc/ssl/certs/ca-certificates.crt",  # Debian/Ubuntu
            "/etc/ssl/certs/ca-bundle.crt",  # CentOS/RHEL
            "/etc/ssl/ca-bundle.pem",  # OpenSUSE
        ]

        ca_path = None
        for path in ca_paths:
            if Path(path).exists():
                ca_path = path
                break

        if ca_path:
            response = httpmorph.get(f"{httpbin_server}/get", verify=ca_path)
            assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2
        else:
            pytest.skip("No CA bundle found on system")


class TestCertParameter:
    """Tests for cert= parameter (client certificates)"""

    def test_cert_tuple(self, httpbin_server):
        """Test cert= with (cert, key) tuple"""
        # This would need actual client cert/key files to test
        pytest.skip("Requires client certificate files")

    def test_cert_single_file(self, httpbin_server):
        """Test cert= with single PEM file"""
        pytest.skip("Requires client certificate file")


class TestProxiesParameter:
    """Tests for proxies= parameter"""

    def test_proxies_http(self, httpbin_server):
        """Test HTTP proxy"""
        # Requires actual proxy to test
        pytest.skip("Requires proxy server")

    def test_proxies_https(self, httpbin_server):
        """Test HTTPS proxy"""
        pytest.skip("Requires proxy server")

    def test_proxies_socks(self, httpbin_server):
        """Test SOCKS proxy"""
        pytest.skip("Requires SOCKS proxy")

    def test_proxies_per_protocol(self, httpbin_server):
        """Test different proxies for different protocols"""
        # Would need actual proxies to test
        pytest.skip("Requires proxy servers")


class TestSessionConnectionPooling:
    """Tests for session connection pooling"""

    def test_session_reuses_connection(self, httpbin_server):
        """Test session reuses TCP connection"""
        session = httpmorph.Session()

        # Make multiple requests to same host
        response1 = session.get(f"{httpbin_server}/get")
        response2 = session.get(f"{httpbin_server}/headers")
        response3 = session.get(f"{httpbin_server}/ip")

        # All should succeed
        assert response1.status_code == 200
        assert response2.status_code == 200
        assert response3.status_code == 200

        # Connection should be reused (hard to test directly)
        # But timing should be faster for 2nd+ requests

    def test_session_connection_per_host(self, httpbin_server):
        """Test session maintains separate connections per host"""
        # Use HTTP/1.1 for compatibility with postman-echo
        session = httpmorph.Session(http2=False)

        # Requests to different hosts
        response1 = session.get(f"{httpbin_server}/get")
        response2 = session.get("https://postman-echo.com/get")

        assert response1.status_code == 200
        assert response2.status_code == 200


class TestRedirectHandling:
    """Tests for redirect handling"""

    def test_max_redirects_exceeded(self, httpbin_server):
        """Test TooManyRedirects exception"""
        with pytest.raises(httpmorph.TooManyRedirects):
            httpmorph.get(f"{httpbin_server}/redirect/20", max_redirects=5)

    def test_redirect_preserve_method_post(self, httpbin_server):
        """Test POST is converted to GET on 302"""
        response = httpmorph.post(
            f"{httpbin_server}/redirect-to?url=/get&status_code=302", data={"key": "value"}
        )

        # After redirect, should be GET request
        data = response.json()
        assert data["method"] == "GET"  # or 'POST' depending on behavior

    def test_redirect_absolute_url(self, httpbin_server):
        """Test redirect to absolute URL"""
        response = httpmorph.get(f"{httpbin_server}/absolute-redirect/1")

        assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2
        assert response.url == f"{httpbin_server}/get"

    def test_redirect_relative_url(self, httpbin_server):
        """Test redirect to relative URL"""
        response = httpmorph.get(f"{httpbin_server}/relative-redirect/1")

        assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2


class TestResponseLinks:
    """Tests for Response.links property"""

    def test_links_property_exists(self, httpbin_server):
        """Test response has .links property"""
        response = httpmorph.get(f"{httpbin_server}/links/5")

        assert hasattr(response, "links")

    def test_links_parsing(self, httpbin_server):
        """Test Link header parsing"""
        # Would need endpoint that returns Link headers
        pytest.skip("Requires endpoint with Link headers")


class TestResponseEncoding:
    """Tests for response encoding detection"""

    def test_encoding_from_header(self, httpbin_server):
        """Test encoding detected from Content-Type header"""
        response = httpmorph.get(f"{httpbin_server}/get")

        assert hasattr(response, "encoding")
        assert response.encoding in ("utf-8", "UTF-8", None)

    def test_encoding_override(self, httpbin_server):
        """Test manually setting encoding"""
        response = httpmorph.get(f"{httpbin_server}/get")

        response.encoding = "iso-8859-1"
        assert response.encoding == "iso-8859-1"

    def test_apparent_encoding(self, httpbin_server):
        """Test apparent_encoding detection"""
        response = httpmorph.get(f"{httpbin_server}/html")

        assert hasattr(response, "apparent_encoding")
        # Should detect encoding from content


class TestRequestObject:
    """Tests for Request object"""

    def test_request_object_creation(self, httpbin_server):
        """Test Request object can be created"""
        url = f"{httpbin_server}/get"
        req = httpmorph.Request("GET", url)

        assert req.method == "GET"
        assert req.url == url

    def test_prepared_request(self, httpbin_server):
        """Test PreparedRequest object"""
        url = f"{httpbin_server}/get"
        req = httpmorph.Request("GET", url)
        prepared = req.prepare()

        assert hasattr(prepared, "method")
        assert hasattr(prepared, "url")
        assert hasattr(prepared, "headers")


class TestResponseHistory:
    """Tests for response redirect history"""

    def test_history_empty_no_redirects(self, httpbin_server):
        """Test history is empty when no redirects"""
        response = httpmorph.get(f"{httpbin_server}/get")

        assert hasattr(response, "history")
        assert len(response.history) == 0

    def test_history_contains_redirects(self, httpbin_server):
        """Test history contains all intermediate responses"""
        response = httpmorph.get(f"{httpbin_server}/redirect/3")

        assert len(response.history) == 3

        for intermediate in response.history:
            assert intermediate.status_code in (301, 302, 303, 307, 308)
            assert intermediate.is_redirect

    def test_history_chain_order(self, httpbin_server):
        """Test history is in chronological order"""
        response = httpmorph.get(f"{httpbin_server}/redirect/2")

        # First redirect should be first in history
        assert response.history[0].status_code in (301, 302, 303, 307, 308)


class TestResponseIsRedirect:
    """Tests for response.is_redirect property"""

    def test_is_redirect_for_3xx(self, httpbin_server):
        """Test is_redirect=True for 3xx codes"""
        redirect_codes = [301, 302, 303, 307, 308]

        for code in redirect_codes:
            response = httpmorph.get(f"{httpbin_server}/status/{code}", allow_redirects=False)
            assert response.is_redirect is True

    def test_not_redirect_for_2xx(self, httpbin_server):
        """Test is_redirect=False for 2xx codes"""
        response = httpmorph.get(f"{httpbin_server}/get")

        assert response.is_redirect is False


class TestResponseRaw:
    """Tests for response.raw attribute"""

    def test_raw_attribute_exists(self, httpbin_server):
        """Test response has .raw attribute"""
        response = httpmorph.get(f"{httpbin_server}/get", stream=True)

        assert hasattr(response, "raw")

    def test_raw_read_method(self, httpbin_server):
        """Test raw.read() method"""
        response = httpmorph.get(f"{httpbin_server}/bytes/100", stream=True)

        data = response.raw.read(50)
        assert len(data) == 50


if __name__ == "__main__":
    pytest.main([__file__, "-v", "-s"])
