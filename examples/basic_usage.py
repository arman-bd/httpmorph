#!/usr/bin/env python3
"""
Basic usage examples for httpmorph

NOTE: These examples will work once the C extension is fully implemented.
For now, they demonstrate the intended API.
"""

import httpmorph


def example_simple_get():
    """Simple GET request"""
    print("=== Simple GET Request ===")

    try:
        response = httpmorph.get("https://httpbin.org/get")
        print(f"Status: {response.status_code}")
        print(f"Body: {response.body[:100]}...")
    except NotImplementedError:
        print("Not yet implemented - C extension needs to be built")


def example_session():
    """Using sessions for persistent fingerprints"""
    print("\n=== Session with Persistent Fingerprint ===")

    try:
        # Create session with Chrome fingerprint
        session = httpmorph.Session(browser="chrome")

        # All requests in this session use the same fingerprint
        response1 = session.get("https://httpbin.org/get")
        response2 = session.get("https://httpbin.org/user-agent")

        print(f"Request 1 - Status: {response1.status_code}")
        print(f"Request 2 - Status: {response2.status_code}")
        print("Same TLS fingerprint maintained across requests")
    except NotImplementedError:
        print("Not yet implemented - C extension needs to be built")


def example_different_browsers():
    """Testing different browser fingerprints"""
    print("\n=== Different Browser Fingerprints ===")

    browsers = ["chrome", "firefox", "safari", "edge"]

    for browser in browsers:
        try:
            httpmorph.Session(browser=browser)
            print(f"Created {browser} session")
            # response = session.get("https://httpbin.org/user-agent")
            # print(f"  User-Agent: {response.headers.get('User-Agent')}")
        except NotImplementedError:
            print(f"  {browser}: Not yet implemented")


def example_post_json():
    """POST request with JSON data"""
    print("\n=== POST Request with JSON ===")

    try:
        data = {
            "username": "test_user",
            "email": "test@example.com",
            "age": 25
        }

        response = httpmorph.post(
            "https://httpbin.org/post",
            json=data,
            headers={"Content-Type": "application/json"}
        )

        print(f"Status: {response.status_code}")
        print(f"Response: {response.body[:200]}...")
    except NotImplementedError:
        print("Not yet implemented - C extension needs to be built")


def example_custom_headers():
    """Request with custom headers"""
    print("\n=== Custom Headers ===")

    try:
        headers = {
            "User-Agent": "CustomBot/1.0",
            "Accept": "application/json",
            "X-Custom-Header": "CustomValue"
        }

        session = httpmorph.Session(browser="chrome")
        response = session.get(
            "https://httpbin.org/headers",
            headers=headers
        )

        print(f"Status: {response.status_code}")
    except NotImplementedError:
        print("Not yet implemented - C extension needs to be built")


def example_rotating_fingerprint():
    """Rotating fingerprints for each request"""
    print("\n=== Rotating Fingerprints ===")

    try:
        for i in range(5):
            # Each request uses a different random browser fingerprint
            response = httpmorph.get(
                "https://httpbin.org/get",
                browser="random",
                rotate_fingerprint=True
            )
            print(f"Request {i+1} - Status: {response.status_code}, "
                  f"JA3: {response.ja3_fingerprint}")
    except NotImplementedError:
        print("Not yet implemented - C extension needs to be built")


def example_performance_test():
    """Simple performance comparison"""
    print("\n=== Performance Test ===")

    import time

    url = "https://httpbin.org/get"
    iterations = 10

    try:
        # Test httpmorph
        start = time.time()
        for _ in range(iterations):
            httpmorph.get(url)
        fast_time = time.time() - start

        print(f"httpmorph: {iterations} requests in {fast_time:.2f}s")
        print(f"Average: {fast_time/iterations*1000:.1f}ms per request")

        # Compare with requests
        try:
            import requests
            start = time.time()
            for _ in range(iterations):
                requests.get(url)
            requests_time = time.time() - start

            print(f"requests: {iterations} requests in {requests_time:.2f}s")
            print(f"Average: {requests_time/iterations*1000:.1f}ms per request")
            print(f"Speedup: {requests_time/fast_time:.2f}x faster")
        except ImportError:
            print("requests library not installed for comparison")

    except NotImplementedError:
        print("Not yet implemented - C extension needs to be built")


def show_library_info():
    """Display library information"""
    print("=== httpmorph Library Information ===")
    print(f"Version: {httpmorph.version()}")
    print("Features:")
    print("  - High-performance C implementation")
    print("  - io_uring support (Linux 5.1+)")
    print("  - BoringSSL for TLS (Chrome-compatible)")
    print("  - Dynamic JA3/JA4 fingerprinting")
    print("  - HTTP/2 and HTTP/3 support")
    print("  - Zero-copy I/O operations")
    print()


if __name__ == "__main__":
    show_library_info()

    # Run examples
    example_simple_get()
    example_session()
    example_different_browsers()
    example_post_json()
    example_custom_headers()
    example_rotating_fingerprint()
    example_performance_test()

    print("\n=== Examples Complete ===")
    print("Note: Most examples require the C extension to be fully implemented.")
    print("Run 'make setup && make install' to build the extension.")
