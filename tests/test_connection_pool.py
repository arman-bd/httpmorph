"""
Connection pool tests for edge cases and bug fixes.

These tests verify connection pooling behavior, particularly around
connection reuse, cleanup, and error handling.
"""

import os
import pytest
import httpmorph

# Load environment variables from .env file
try:
    from dotenv import load_dotenv
    load_dotenv()
except ImportError:
    # python-dotenv not installed, skip - env vars should be set directly in CI
    pass

# Use TEST_HTTPBIN_HOST environment variable
HTTPBIN_HOST = os.environ.get('TEST_HTTPBIN_HOST')
if not HTTPBIN_HOST:
    raise ValueError("TEST_HTTPBIN_HOST environment variable is not set. Please check .env file.")


# =============================================================================
# CONNECTION POOL CLEANUP TESTS
# =============================================================================

@pytest.mark.integration
def test_connection_pool_empty_body_after_multiple_requests():
    """
    Test for SSL double-free bug fix (Issue #33).

    This test reproduces the crash scenario where making 4+ requests with
    non-empty bodies followed by a request with an empty body would cause
    a SIGABRT due to double-free of the SSL object.

    The bug occurred because:
    1. Server returns Connection: close for empty body response
    2. pool_connection_destroy() frees the SSL object
    3. Local ssl variable still holds reference to freed object
    4. Cleanup code attempts to free the same SSL object again -> SIGABRT

    With the fix, the local ssl/sockfd references are cleared after
    pool_connection_destroy(), preventing the double-free.
    """
    session = httpmorph.Session()

    # Make 4 requests with non-empty bodies to establish connection pool
    for i in range(1, 5):
        response = session.get(f"https://{HTTPBIN_HOST}/get", timeout=30)
        assert response.status_code == 200
        assert len(response.body) > 0, f"Request {i} should have non-empty body"

    # Make a 5th request that returns empty body
    # This should trigger Connection: close and proper cleanup
    # Without the fix, this would crash with SIGABRT
    response = session.get(f"https://{HTTPBIN_HOST}/status/200", timeout=30)
    assert response.status_code == 200
    assert len(response.body) == 0, "Status endpoint should return empty body"

    # If we get here, the bug is fixed!
    # Make one more request to verify connection pool still works
    response = session.get(f"https://{HTTPBIN_HOST}/get", timeout=30)
    assert response.status_code == 200


@pytest.mark.integration
def test_connection_pool_multiple_empty_body_requests():
    """
    Test that multiple consecutive empty body requests don't cause issues.

    This ensures the connection pool cleanup works correctly even when
    multiple requests in a row trigger connection closure.
    """
    session = httpmorph.Session()

    # Make multiple empty body requests
    for i in range(5):
        response = session.get(f"https://{HTTPBIN_HOST}/status/204", timeout=30)
        assert response.status_code == 204
        assert len(response.body) == 0


@pytest.mark.integration
def test_connection_pool_alternating_body_sizes():
    """
    Test connection pool with alternating request patterns.

    This tests various combinations of empty and non-empty bodies
    to ensure robust connection pool cleanup.
    """
    session = httpmorph.Session()

    patterns = [
        (f"https://{HTTPBIN_HOST}/get", True),           # Has body
        (f"https://{HTTPBIN_HOST}/status/200", False),   # Empty body
        (f"https://{HTTPBIN_HOST}/get", True),           # Has body
        (f"https://{HTTPBIN_HOST}/status/204", False),   # Empty body
        (f"https://{HTTPBIN_HOST}/bytes/100", True),     # Has body
        (f"https://{HTTPBIN_HOST}/status/200", False),   # Empty body
    ]

    for url, expects_body in patterns:
        response = session.get(url, timeout=30)
        assert response.status_code in [200, 204]
        if expects_body:
            assert len(response.body) > 0
        else:
            assert len(response.body) == 0


@pytest.mark.integration
def test_connection_pool_reuse_after_close():
    """
    Test that connection pool properly creates new connections after
    a connection is closed.

    This verifies that after a Connection: close response, subsequent
    requests can still succeed by creating new connections.
    """
    session = httpmorph.Session()

    # Establish connection
    response = session.get(f"https://{HTTPBIN_HOST}/get", timeout=30)
    assert response.status_code == 200

    # Trigger connection close
    response = session.get(f"https://{HTTPBIN_HOST}/status/200", timeout=30)
    assert response.status_code == 200

    # New request should create new connection and work fine
    response = session.get(f"https://{HTTPBIN_HOST}/get", timeout=30)
    assert response.status_code == 200
    assert len(response.body) > 0


@pytest.mark.integration
def test_connection_pool_stress_with_empty_bodies():
    """
    Stress test with many requests including empty bodies.

    This ensures connection pool remains stable under load with
    mixed request patterns.
    """
    session = httpmorph.Session()

    # Mix of requests with and without bodies
    for cycle in range(3):
        # Batch of normal requests
        for _ in range(5):
            response = session.get(f"https://{HTTPBIN_HOST}/get", timeout=30)
            assert response.status_code == 200

        # Empty body request
        response = session.get(f"https://{HTTPBIN_HOST}/status/200", timeout=30)
        assert response.status_code == 200
