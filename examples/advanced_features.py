#!/usr/bin/env python3
"""
httpmorph - Advanced Features Demo

Demonstrates all implemented features:
- Browser profiles with different JA3 fingerprints
- TLS information extraction
- Session management with cookies
- Custom headers
- Response timing
- Gzip decompression
"""

import httpmorph


def demo_browser_profiles():
    """Demo different browser profiles"""
    print("\n" + "=" * 60)
    print("1. Browser Profiles")
    print("=" * 60)

    browsers = ['chrome', 'firefox', 'safari', 'edge']

    for browser in browsers:
        session = httpmorph.Session(browser=browser)
        response = session.get("https://example.com")

        print(f"\n{browser.upper()}:")
        print(f"  Status:      {response.status_code}")
        print(f"  User-Agent:  {session.user_agent[:50]}...")
        print(f"  JA3 Hash:    {response.ja3_fingerprint}")
        print(f"  TLS Version: {response.tls_version}")
        print(f"  TLS Cipher:  {response.tls_cipher}")


def demo_tls_information():
    """Demo TLS information extraction"""
    print("\n" + "=" * 60)
    print("2. TLS Information Extraction")
    print("=" * 60)

    response = httpmorph.get("https://example.com")

    print("\nTLS Details:")
    print(f"  Version:     {response.tls_version}")
    print(f"  Cipher:      {response.tls_cipher}")
    print(f"  JA3:         {response.ja3_fingerprint}")
    print(f"  HTTP Ver:    {response.http_version}")


def demo_session_management():
    """Demo session with cookies and custom headers"""
    print("\n" + "=" * 60)
    print("3. Session Management")
    print("=" * 60)

    with httpmorph.Session(browser='chrome') as session:
        # Custom headers
        headers = {
            'X-Custom-Header': 'MyValue',
            'Accept': 'application/json'
        }

        response = session.get("https://api.github.com", headers=headers)

        print("\nGitHub API Request:")
        print(f"  Status:      {response.status_code}")
        print(f"  Body length: {len(response.body)} bytes")
        print(f"  Time taken:  {response.total_time_us / 1000:.2f}ms")


def demo_http_methods():
    """Demo different HTTP methods"""
    print("\n" + "=" * 60)
    print("4. HTTP Methods")
    print("=" * 60)

    # GET request
    response = httpmorph.get("https://example.com")
    print("\nGET Request:")
    print(f"  Status: {response.status_code}")
    print(f"  Time:   {response.total_time_us / 1000:.2f}ms")

    # POST with JSON (would work with httpbin if not rate limited)
    # data = {"key": "value", "number": 42}
    # response = httpmorph.post("https://httpbin.org/post", json=data)


def demo_performance_metrics():
    """Demo performance metrics"""
    print("\n" + "=" * 60)
    print("5. Performance Metrics")
    print("=" * 60)

    import time

    session = httpmorph.Session(browser='chrome')

    # Multiple requests to same domain
    urls = [
        "https://example.com",
        "https://www.google.com",
        "https://api.github.com"
    ]

    print("\nSequential Requests:")
    for url in urls:
        start = time.time()
        response = session.get(url)
        elapsed = (time.time() - start) * 1000

        print(f"  {url:30} - {response.status_code} ({elapsed:.2f}ms)")


def demo_compression():
    """Demo automatic gzip decompression"""
    print("\n" + "=" * 60)
    print("6. Automatic Gzip Decompression")
    print("=" * 60)

    response = httpmorph.get("https://example.com")

    print("\nResponse:")
    print(f"  Encoding:    {response.headers.get('Content-Encoding', 'none')}")
    print(f"  Body length: {len(response.body)} bytes")
    print(f"  Text length: {len(response.text)} chars")
    print(f"  Decoded:     {'Yes' if response.text else 'No'}")


def main():
    """Run all demos"""
    print("\n" + "=" * 60)
    print("httpmorph - Advanced Features Demo")
    print("=" * 60)

    try:
        demo_browser_profiles()
        demo_tls_information()
        demo_session_management()
        demo_http_methods()
        demo_performance_metrics()
        demo_compression()

        print("\n" + "=" * 60)
        print("✅ All features working successfully!")
        print("=" * 60)
        print()

    except Exception as e:
        print(f"\n❌ Error: {e}")
        import traceback
        traceback.print_exc()


if __name__ == "__main__":
    main()
