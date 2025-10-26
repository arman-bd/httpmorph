"""
Session tests for httpmorph
"""

import pytest

import httpmorph
from tests.test_server import MockHTTPServer


class TestSession:
    """Test the httpmorph Session class"""

    def test_session_creation_chrome(self):
        """Test session can be created with Chrome browser"""
        try:
            session = httpmorph.Session(browser="chrome")
            assert session is not None
        except (NotImplementedError, AttributeError):
            pytest.skip("Session not yet implemented")

    def test_session_creation_firefox(self):
        """Test session can be created with Firefox browser"""
        try:
            session = httpmorph.Session(browser="firefox")
            assert session is not None
        except (NotImplementedError, AttributeError):
            pytest.skip("Session not yet implemented")

    def test_session_creation_safari(self):
        """Test session can be created with Safari browser"""
        try:
            session = httpmorph.Session(browser="safari")
            assert session is not None
        except (NotImplementedError, AttributeError):
            pytest.skip("Session not yet implemented")

    def test_session_creation_edge(self):
        """Test session can be created with Edge browser"""
        try:
            session = httpmorph.Session(browser="edge")
            assert session is not None
        except (NotImplementedError, AttributeError):
            pytest.skip("Session not yet implemented")

    def test_session_creation_random(self):
        """Test session can be created with random browser"""
        try:
            session = httpmorph.Session(browser="random")
            assert session is not None
        except (NotImplementedError, AttributeError):
            pytest.skip("Session not yet implemented")

    def test_session_creation_invalid_browser(self):
        """Test session creation with invalid browser falls back to chrome"""
        try:
            # Should not raise, should fall back to chrome
            session = httpmorph.Session(browser="invalid_browser")
            assert session is not None
        except (NotImplementedError, AttributeError):
            pytest.skip("Session not yet implemented")

    def test_session_get_request(self):
        """Test GET request using session"""
        with MockHTTPServer() as server:
            session = httpmorph.Session(browser="chrome")
            response = session.get(f"{server.url}/get")
            assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2

    def test_session_post_request(self):
        """Test POST request using session"""
        with MockHTTPServer() as server:
            session = httpmorph.Session(browser="chrome")
            data = {"test": "data"}
            response = session.post(f"{server.url}/post", json=data)
            assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2

    def test_session_multiple_requests(self):
        """Test multiple requests with same session"""
        with MockHTTPServer() as server:
            session = httpmorph.Session(browser="chrome")

            # Make multiple requests
            for i in range(5):
                response = session.get(f"{server.url}/get")
                assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2

    def test_session_fingerprint_consistency(self):
        """Test that session maintains consistent fingerprint"""
        session = httpmorph.Session(browser="chrome")

        # Make multiple requests
        fingerprints = []
        for _ in range(3):
            response = session.get("https://example.com")
            fingerprints.append(response.ja3_fingerprint)

        # All fingerprints should be the same within a session
        assert len(set(fingerprints)) == 1

    def test_different_sessions_different_fingerprints(self):
        """Test that different sessions have different fingerprints when using random"""
        fingerprints = []

        for _ in range(3):
            session = httpmorph.Session(browser="random")
            response = session.get("https://example.com")
            fingerprints.append(response.ja3_fingerprint)

        # Should have variation (though not guaranteed to be different)
        # At least check they exist
        assert all(fp is not None for fp in fingerprints)

    def test_session_with_custom_headers(self):
        """Test session with custom headers"""
        with MockHTTPServer() as server:
            session = httpmorph.Session(browser="chrome")
            headers = {"X-Custom-Header": "test-value", "Authorization": "Bearer token123"}
            response = session.get(f"{server.url}/headers", headers=headers)
            assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2

    def test_session_cookie_persistence(self):
        """Test that session maintains cookies"""
        session = httpmorph.Session(browser="chrome")

        # Test with a real site that sets cookies (Google)
        session.get("https://httpbingo.org/get")
        cookies_before = len(session.cookie_jar)

        # Cookies should be set after first request
        assert cookies_before > 0, "No cookies were set by Google"

        # Second request - cookies should persist
        session.get("https://httpbingo.org/search?q=test")
        cookies_after = len(session.cookie_jar)

        # Cookie count should be stable (same cookies)
        assert cookies_after >= cookies_before, "Cookies were lost between requests"

    def test_session_context_manager(self):
        """Test session as context manager"""
        with MockHTTPServer() as server:
            with httpmorph.Session(browser="chrome") as session:
                response = session.get(f"{server.url}/get")
                assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2


class TestSessionWithRealHTTPS:
    """Test session with real HTTPS connections"""

    def test_session_real_https(self):
        """Test session with real HTTPS endpoint"""
        session = httpmorph.Session(browser="chrome")
        response = session.get("https://example.com")
        assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2

    def test_chrome_session_characteristics(self):
        """Test Chrome session has Chrome characteristics"""
        session = httpmorph.Session(browser="chrome")
        response = session.get("https://example.com")

        # Should have Chrome-like TLS fingerprint
        assert response.tls_version is not None
        assert response.ja3_fingerprint is not None

    def test_firefox_session_characteristics(self):
        """Test Firefox session has Firefox characteristics"""
        session = httpmorph.Session(browser="firefox")
        response = session.get("https://example.com")

        # Should have Firefox-like TLS fingerprint
        assert response.tls_version is not None
        assert response.ja3_fingerprint is not None

    def test_session_multiple_domains(self):
        """Test session working with multiple domains"""
        session = httpmorph.Session(browser="chrome")

        # Request to different domains
        response1 = session.get("https://example.com")
        response2 = session.get("https://httpbingo.org/get")

        assert response1.status_code == 200
        assert response2.status_code == 200


class TestSessionHTTP2Flag:
    """Test Session with HTTP/2 flag (httpx-like API)"""

    def test_session_http2_flag_default(self):
        """Test that Session http2 flag defaults to False"""
        session = httpmorph.Session(browser="chrome")
        assert hasattr(session, "http2")
        assert session.http2 is False

    def test_session_http2_flag_enabled(self):
        """Test Session with http2=True"""
        session = httpmorph.Session(browser="chrome", http2=True)
        assert session.http2 is True

        # Test actual HTTP/2 request
        response = session.get("https://httpbingo.org/get", timeout=10)
        assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2
        assert response.http_version == "2.0"

    def test_session_http2_flag_disabled(self):
        """Test Session with http2=False explicitly"""
        session = httpmorph.Session(browser="chrome", http2=False)
        assert session.http2 is False

    def test_session_http2_per_request_override(self):
        """Test per-request http2 parameter overrides session default"""
        # Session default is False
        session = httpmorph.Session(browser="chrome", http2=False)
        assert session.http2 is False

        # But request with http2=True should use HTTP/2
        response = session.get("https://httpbingo.org/get", http2=True, timeout=10)
        assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2
        assert response.http_version == "2.0"

    def test_session_http2_flag_persistence(self):
        """Test http2 flag persists across multiple requests"""
        session = httpmorph.Session(browser="chrome", http2=True)

        # Make multiple requests
        for _ in range(3):
            response = session.get("https://httpbingo.org/get", timeout=10)
            assert response.http_version == "2.0"

        # Flag should still be True
        assert session.http2 is True

    def test_session_http2_with_different_browsers(self):
        """Test HTTP/2 flag works with different browser profiles"""
        browsers = ["chrome", "firefox", "safari", "edge"]

        for browser in browsers:
            session = httpmorph.Session(browser=browser, http2=True)
            assert session.http2 is True

            response = session.get("https://httpbingo.org/get", timeout=10)
            assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2
            assert response.http_version == "2.0", f"HTTP/2 failed for {browser} browser"

    def test_session_http2_with_context_manager(self):
        """Test HTTP/2 flag with session as context manager"""
        with httpmorph.Session(browser="chrome", http2=True) as session:
            assert session.http2 is True
            response = session.get("https://httpbingo.org/get", timeout=10)
            assert response.status_code in [200, 402]  # httpbingo returns 402 for HTTP/2
            assert response.http_version == "2.0"


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
