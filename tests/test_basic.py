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


def test_post_with_json(httpbin_server):
    """Test POST request with JSON data"""
    data = {"key": "value", "number": 42}
    response = httpmorph.post(f"{httpbin_server}/post", json=data)

    assert response.status_code == 200

    # Test compatibility: should work with both .body and .json()
    if hasattr(response, "json"):
        response_data = response.json()
    else:
        import json

        response_data = json.loads(response.body)

    assert response_data["json"] == data


def test_fingerprint_rotation():
    """Test that fingerprints are rotated correctly"""
    _session1 = httpmorph.Session(browser="chrome")
    _session2 = httpmorph.Session(browser="firefox")

    # Sessions with different browsers should have different fingerprints
    # This will be testable once we implement fingerprint tracking


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
