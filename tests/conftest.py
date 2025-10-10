"""
Pytest configuration and fixtures for httpmorph tests
"""

import subprocess
import time
from pathlib import Path

import filelock
import pytest

import httpmorph


@pytest.fixture(scope="session", autouse=True)
def initialize_httpmorph():
    """Initialize httpmorph library before running tests"""
    try:
        httpmorph.init()
        yield
        httpmorph.cleanup()
    except (NotImplementedError, AttributeError):
        # If init/cleanup not implemented, just skip
        yield


@pytest.fixture
def http_server():
    """Create a test HTTP server"""
    from tests.test_server import MockHTTPServer

    server = MockHTTPServer()
    server.start()
    yield server
    server.stop()


@pytest.fixture
def https_server():
    """Create a test HTTPS server"""
    from tests.test_server import MockHTTPServer

    try:
        server = MockHTTPServer(ssl_enabled=True)
        server.start()
        yield server
        server.stop()
    except RuntimeError as e:
        pytest.skip(f"HTTPS server not available: {e}")


@pytest.fixture
def chrome_session():
    """Create a Chrome session"""
    try:
        session = httpmorph.Session(browser="chrome")
        yield session
    except (NotImplementedError, AttributeError):
        pytest.skip("Session not yet implemented")


@pytest.fixture
def firefox_session():
    """Create a Firefox session"""
    try:
        session = httpmorph.Session(browser="firefox")
        yield session
    except (NotImplementedError, AttributeError):
        pytest.skip("Session not yet implemented")


@pytest.fixture
def safari_session():
    """Create a Safari session"""
    try:
        session = httpmorph.Session(browser="safari")
        yield session
    except (NotImplementedError, AttributeError):
        pytest.skip("Session not yet implemented")


@pytest.fixture(scope="session")
def httpbin_server():
    """Use MockHTTPServer for httpbin-compatible testing"""
    from tests.test_server import MockHTTPServer

    server = MockHTTPServer()
    server.start()
    yield server.url
    server.stop()


@pytest.fixture(scope="session")
def mock_httpbin_server():
    """Use MockHTTPServer for tests where Docker httpbin fails"""
    from tests.test_server import MockHTTPServer

    server = MockHTTPServer()
    server.start()
    yield server.url
    server.stop()


def pytest_configure(config):
    """Configure pytest"""
    config.addinivalue_line(
        "markers", "integration: mark test as integration test (requires network)"
    )
    config.addinivalue_line("markers", "slow: mark test as slow running")
    config.addinivalue_line("markers", "ssl: mark test as requiring SSL support")


def pytest_collection_modifyitems(config, items):
    """Modify test collection"""
    # Add markers based on test location
    for item in items:
        if "test_integration" in item.nodeid:
            item.add_marker(pytest.mark.integration)
        if "test_browser_profiles" in item.nodeid:
            item.add_marker(pytest.mark.slow)
        if "https_server" in item.fixturenames or "ssl" in item.nodeid:
            item.add_marker(pytest.mark.ssl)
