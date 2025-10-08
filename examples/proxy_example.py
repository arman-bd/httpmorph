#!/usr/bin/env python3
"""
Example: Using httpmorph with HTTP/HTTPS proxy

This demonstrates:
- Loading proxy configuration from .env file
- Making requests through a proxy
- Proxy authentication
- Testing proxy connectivity

Setup:
1. Create a .env file in this directory with:
   PROXY_URL=http://proxy.example.com:8080
   PROXY_USERNAME=your_username
   PROXY_PASSWORD=your_password

2. Install python-dotenv:
   pip install python-dotenv

3. Run this script:
   python proxy_example.py
"""

import os
import sys
from pathlib import Path

try:
    from dotenv import load_dotenv
except ImportError:
    print("ERROR: python-dotenv not installed")
    print("Install with: pip install python-dotenv")
    sys.exit(1)

# Add parent directory to path for importing httpmorph
sys.path.insert(0, str(Path(__file__).parent.parent))

import httpmorph  # noqa: E402


def load_proxy_config():
    """Load proxy configuration from .env file"""
    # Look for .env in current directory and examples directory
    env_paths = [
        Path.cwd() / '.env',
        Path(__file__).parent / '.env',
        Path(__file__).parent.parent / '.env',
    ]

    env_file = None
    for path in env_paths:
        if path.exists():
            env_file = path
            break

    if env_file:
        print(f"Loading proxy config from: {env_file}")
        load_dotenv(env_file)
    else:
        print("No .env file found. Create one with PROXY_URL, PROXY_USERNAME, PROXY_PASSWORD")
        print(f"Searched in: {[str(p) for p in env_paths]}")

    proxy_url = os.getenv('PROXY_URL')
    proxy_username = os.getenv('PROXY_USERNAME')
    proxy_password = os.getenv('PROXY_PASSWORD')

    return proxy_url, proxy_username, proxy_password


def test_direct_connection():
    """Test connection without proxy"""
    print("\n" + "="*60)
    print("TEST 1: Direct Connection (no proxy)")
    print("="*60)

    try:
        # Use example.com as it's more reliable than httpbin
        response = httpmorph.get("https://example.com", timeout=10)

        if response.status_code in [200, 301, 302]:
            print(f"✓ Status: {response.status_code}")
            print(f"✓ Response size: {len(response.body)} bytes")
            print("✓ Direct connection working")
            return True
        elif response.status_code == 0:
            print("✗ Connection failed")
            if response.error_message:
                print(f"  Error: {response.error_message}")
            return False
        else:
            print(f"✗ Failed with status: {response.status_code}")
            return False
    except Exception as e:
        print(f"✗ Error: {e}")
        return False


def test_proxy_http(proxy_url, username=None, password=None):
    """Test HTTP request through proxy"""
    print("\n" + "="*60)
    print("TEST 2: HTTP Request via Proxy")
    print("="*60)
    print(f"Proxy: {proxy_url}")
    if username:
        print(f"Auth: {username}:{'*' * len(password) if password else ''}")

    try:
        kwargs = {'proxy': proxy_url, 'timeout': 10}
        if username and password:
            kwargs['proxy_auth'] = (username, password)

        response = httpmorph.get("http://icanhazip.com", **kwargs)

        if response.status_code == 200:
            print(f"✓ Status: {response.status_code}")
            ip = response.body.decode('utf-8').strip()
            print(f"✓ Your IP via proxy: {ip}")
            return True
        elif response.status_code == 0:
            print("✗ Connection failed")
            if response.error_message:
                print(f"  Error: {response.error_message}")
            return False
        else:
            print(f"✗ HTTP {response.status_code}")
            return False
    except Exception as e:
        print(f"✗ Exception: {e}")
        return False


def test_proxy_https(proxy_url, username=None, password=None):
    """Test HTTPS request through proxy (uses CONNECT method)"""
    print("\n" + "="*60)
    print("TEST 3: HTTPS Request via Proxy (CONNECT tunnel)")
    print("="*60)
    print(f"Proxy: {proxy_url}")
    if username:
        print(f"Auth: {username}:{'*' * len(password) if password else ''}")

    try:
        kwargs = {'proxy': proxy_url, 'timeout': 10}
        if username and password:
            kwargs['proxy_auth'] = (username, password)

        response = httpmorph.get("https://icanhazip.com", **kwargs)

        if response.status_code == 200:
            print(f"✓ Status: {response.status_code}")
            ip = response.body.decode('utf-8').strip()
            print(f"✓ Your IP via proxy: {ip}")
            print(f"✓ TLS Version: {response.tls_version}")
            print(f"✓ TLS Cipher: {response.tls_cipher}")
            return True
        elif response.status_code == 0:
            print("✗ Connection failed")
            if response.error_message:
                print(f"  Error: {response.error_message}")
            return False
        else:
            print(f"✗ HTTP {response.status_code}")
            return False
    except Exception as e:
        print(f"✗ Exception: {e}")
        return False


def test_proxy_dict_format(proxy_url, username=None, password=None):
    """Test proxy using requests-style dict format"""
    print("\n" + "="*60)
    print("TEST 4: Proxy Dict Format (requests-compatible)")
    print("="*60)

    proxies = {
        'http': proxy_url,
        'https': proxy_url
    }
    print(f"Proxies: {proxies}")

    try:
        kwargs = {'proxies': proxies, 'timeout': 10}
        if username and password:
            kwargs['proxy_auth'] = (username, password)

        response = httpmorph.get("https://icanhazip.com", **kwargs)

        if response.status_code == 200:
            print(f"✓ Status: {response.status_code}")
            ip = response.body.decode('utf-8').strip()
            print(f"✓ Your IP via proxy: {ip}")
            return True
        elif response.status_code == 0:
            print("✗ Connection failed")
            return False
        else:
            print(f"✗ HTTP {response.status_code}")
            return False
    except Exception as e:
        print(f"✗ Exception: {e}")
        return False


def test_multiple_requests(proxy_url, username=None, password=None):
    """Test multiple requests through same proxy"""
    print("\n" + "="*60)
    print("TEST 5: Multiple Requests via Proxy")
    print("="*60)

    urls = [
        "https://icanhazip.com",
        "https://example.com",
        "https://www.google.com"
    ]

    kwargs = {'proxy': proxy_url, 'timeout': 10}
    if username and password:
        kwargs['proxy_auth'] = (username, password)

    success_count = 0
    for url in urls:
        try:
            response = httpmorph.get(url, **kwargs)
            if response.status_code == 200:
                print(f"✓ {url}: {response.status_code}")
                success_count += 1
            else:
                print(f"✗ {url}: {response.status_code}")
        except Exception as e:
            print(f"✗ {url}: {e}")

    print(f"\nResults: {success_count}/{len(urls)} successful")
    return success_count == len(urls)


def test_ip_comparison(proxy_url, username=None, password=None):
    """Test IP comparison with and without proxy for both HTTP and HTTPS"""
    print("\n" + "="*60)
    print("TEST 6: IP Comparison (Direct vs Proxy)")
    print("="*60)

    try:
        kwargs = {'proxy': proxy_url, 'timeout': 10}
        if username and password:
            kwargs['proxy_auth'] = (username, password)

        # Test HTTPS
        print("\n[HTTPS Test]")
        print("Getting IP without proxy (HTTPS)...")
        direct_response_https = httpmorph.get("https://api.ipify.org", timeout=10)

        if direct_response_https.status_code != 200:
            print("✗ Failed to get direct IP (HTTPS)")
            return False

        direct_ip_https = direct_response_https.body.decode('utf-8').strip()
        print(f"  Direct IP: {direct_ip_https}")

        print("Getting IP via proxy (HTTPS)...")
        proxy_response_https = httpmorph.get("https://api.ipify.org", **kwargs)

        if proxy_response_https.status_code != 200:
            print("✗ Failed to get proxy IP (HTTPS)")
            return False

        proxy_ip_https = proxy_response_https.body.decode('utf-8').strip()
        print(f"  Proxy IP: {proxy_ip_https}")

        https_different = direct_ip_https != proxy_ip_https
        if https_different:
            print("✓ HTTPS: IPs are different (proxy working)")
        else:
            print("⚠️  HTTPS: IPs are the same")

        # Test HTTP
        print("\n[HTTP Test]")
        print("Getting IP without proxy (HTTP)...")
        direct_response_http = httpmorph.get("http://api.ipify.org", timeout=10)

        if direct_response_http.status_code != 200:
            print("✗ Failed to get direct IP (HTTP)")
            return False

        direct_ip_http = direct_response_http.body.decode('utf-8').strip()
        print(f"  Direct IP: {direct_ip_http}")

        print("Getting IP via proxy (HTTP)...")
        proxy_response_http = httpmorph.get("http://api.ipify.org", **kwargs)

        if proxy_response_http.status_code != 200:
            print("✗ Failed to get proxy IP (HTTP)")
            return False

        proxy_ip_http = proxy_response_http.body.decode('utf-8').strip()
        print(f"  Proxy IP: {proxy_ip_http}")

        http_different = direct_ip_http != proxy_ip_http
        if http_different:
            print("✓ HTTP: IPs are different (proxy working)")
        else:
            print("⚠️  HTTP: IPs are the same")

        # Summary
        if https_different and http_different:
            print("\n✓ Both HTTP and HTTPS proxy working correctly")
            print(f"  Direct: {direct_ip_https}")
            print(f"  Via Proxy: {proxy_ip_https}")
            return True
        elif https_different or http_different:
            print("\n⚠️  Warning: Only one protocol showing different IP")
            return False
        else:
            print("\n⚠️  Warning: IPs are the same for both protocols (proxy may not be working)")
            return False

    except Exception as e:
        print(f"✗ Exception: {e}")
        return False


def test_proxy_with_post(proxy_url, username=None, password=None):
    """Test POST request through proxy"""
    print("\n" + "="*60)
    print("TEST 7: POST Request via Proxy")
    print("="*60)

    payload = {
        "message": "Testing httpmorph proxy",
        "timestamp": "2025-01-08"
    }

    kwargs = {'proxy': proxy_url, 'timeout': 10}
    if username and password:
        kwargs['proxy_auth'] = (username, password)

    try:
        response = httpmorph.post(
            "https://httpbingo.org/post",
            json=payload,
            **kwargs
        )

        if response.status_code == 200:
            print(f"✓ Status: {response.status_code}")
            print("✓ Posted data via proxy successfully")
            # Try to parse and show response
            try:
                import json
                data = json.loads(response.body.decode('utf-8'))
                if 'json' in data:
                    print(f"✓ Server received: {data['json']}")
            except Exception:
                pass
            return True
        else:
            print(f"✗ HTTP {response.status_code}")
            return False
    except Exception as e:
        print(f"✗ Exception: {e}")
        return False


def main():
    """Main test function"""
    print("="*60)
    print("httpmorph Proxy Example")
    print("="*60)

    # Load proxy configuration
    proxy_url, username, password = load_proxy_config()

    if not proxy_url:
        print("\n⚠️  No proxy configured in .env file")
        print("\nCreate a .env file with:")
        print("  PROXY_URL=http://your-proxy:8080")
        print("  PROXY_USERNAME=your_username  # optional")
        print("  PROXY_PASSWORD=your_password  # optional")
        print("\nRunning tests without proxy...")
        print()
    else:
        print(f"\n✓ Proxy URL: {proxy_url}")
        if username:
            print(f"✓ Username: {username}")
            print(f"✓ Password: {'*' * len(password)}")

    # Run tests
    results = []

    # Test 1: Direct connection (baseline)
    results.append(("Direct Connection", test_direct_connection()))

    if proxy_url:
        # Test 2: HTTP via proxy
        results.append(("HTTP via Proxy", test_proxy_http(proxy_url, username, password)))

        # Test 3: HTTPS via proxy
        results.append(("HTTPS via Proxy", test_proxy_https(proxy_url, username, password)))

        # Test 4: Dict format
        results.append(("Proxy Dict Format", test_proxy_dict_format(proxy_url, username, password)))

        # Test 5: Multiple requests
        results.append(("Multiple Requests", test_multiple_requests(proxy_url, username, password)))

        # Test 6: IP comparison
        results.append(("IP Comparison", test_ip_comparison(proxy_url, username, password)))

        # Test 7: POST request
        results.append(("POST via Proxy", test_proxy_with_post(proxy_url, username, password)))

    # Summary
    print("\n" + "="*60)
    print("SUMMARY")
    print("="*60)

    for test_name, success in results:
        status = "✓ PASS" if success else "✗ FAIL"
        print(f"{status:8} {test_name}")

    passed = sum(1 for _, s in results if s)
    total = len(results)
    print(f"\nTotal: {passed}/{total} tests passed")

    if passed == total:
        print("\n🎉 All tests passed!")
        return 0
    else:
        print(f"\n⚠️  {total - passed} test(s) failed")
        return 1


if __name__ == "__main__":
    sys.exit(main())
