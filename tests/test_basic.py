"""
Basic tests for httpmorph
"""

import pytest

import httpmorph


def test_import():
    """Test httpmorph can be imported"""
    assert httpmorph is not None


def test_version():
    """Test library version"""
    try:
        version = httpmorph.version()
        assert version is not None
        assert isinstance(version, str)
        print(f"httpmorph version: {version}")
    except (NotImplementedError, AttributeError):
        pytest.skip("version() not yet implemented")


def test_init_cleanup():
    """Test library initialization and cleanup"""
    try:
        httpmorph.init()
        httpmorph.cleanup()
    except (NotImplementedError, AttributeError):
        pytest.skip("init/cleanup not yet implemented")


def test_client_creation():
    """Test client can be created"""
    try:
        client = httpmorph.Client()
        assert client is not None
    except (NotImplementedError, AttributeError):
        pytest.skip("Client not yet implemented")


def test_session_creation():
    """Test session can be created with different browsers"""
    try:
        for browser in ["chrome", "firefox", "safari", "edge", "random"]:
            session = httpmorph.Session(browser=browser)
            assert session is not None
    except (NotImplementedError, AttributeError):
        pytest.skip("Session not yet implemented")


def test_simple_get():
    """Test simple GET request"""
    response = httpmorph.get("https://ipapi.co/json/")
    assert response.status_code == 200
    assert response.body is not None


def test_post_with_json():
    """Test POST request with JSON data"""
    import json

    data = {"key": "value", "number": 42}
    response = httpmorph.post(
        "https://postman-echo.com/post", json=data, headers={"Content-Type": "application/json"}
    )

    assert response.status_code == 200
    response_data = json.loads(response.body)
    assert response_data["json"] == data


def test_fingerprint_rotation():
    """Test that fingerprints are rotated correctly"""
    _session1 = httpmorph.Session(browser="chrome")
    _session2 = httpmorph.Session(browser="firefox")

    # Sessions with different browsers should have different fingerprints
    # This will be testable once we implement fingerprint tracking


def test_performance():
    """Test performance compared to requests library"""
    import time

    from tests.test_server import MockHTTPServer

    iterations = 50  # Reduced for local testing

    with MockHTTPServer() as server:
        url = f"{server.url}/get"

        # Test httpmorph
        start = time.time()
        for _ in range(iterations):
            httpmorph.get(url)
        httpmorph_time = time.time() - start

        # Test requests
        import requests

        start = time.time()
        for _ in range(iterations):
            requests.get(url)
        requests_time = time.time() - start

        print(f"httpmorph: {httpmorph_time:.2f}s")
        print(f"requests: {requests_time:.2f}s")
        if httpmorph_time > 0:
            print(f"Speedup: {requests_time / httpmorph_time:.2f}x")

        # Assert httpmorph completes successfully
        assert httpmorph_time > 0
    assert httpmorph_time < requests_time


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
