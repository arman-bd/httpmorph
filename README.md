# httpmorph

![Build](https://github.com/arman-bd/httpmorph/workflows/CI/badge.svg) [![codecov](https://codecov.io/gh/arman-bd/httpmorph/graph/badge.svg?token=D7BCC52PQN)](https://codecov.io/gh/arman-bd/httpmorph) [![PyPI version](https://badge.fury.io/py/httpmorph.svg)](https://pypi.org/project/httpmorph/) [![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A Python HTTP client focused on mimicking browser fingerprints.

**⚠️ Work in Progress** - This library is in early development. Features and API may change.

## Features

- **Requests-compatible API** - Drop-in replacement for most Python `requests` use cases
- **High Performance** - Native C implementation with BoringSSL for blazing-fast HTTP/HTTPS
- **HTTP/2 Support** - Full HTTP/2 with ALPN negotiation via nghttp2 (httpx-like API)
- **Browser Fingerprinting** - Realistic browser profiles (Chrome, Firefox, Safari, Edge)
- **TLS Fingerprinting** - JA3 fingerprint generation and customization
- **Connection Pooling** - Automatic connection reuse for better performance
- **Session Management** - Persistent cookies and headers across requests

## Installation

```bash
pip install httpmorph
```

### Requirements

- Python 3.8+
- Windows, macOS, or Linux
- BoringSSL (built automatically from source)
- libnghttp2 (for HTTP/2)

## Quick Start

```python
import httpmorph

# Simple GET request
response = httpmorph.get('https://icanhazip.com')
print(response.status_code)
print(response.text)

# POST with JSON
response = httpmorph.post(
    'https://httpbin.org/post',
    json={'key': 'value'}
)

# Using sessions
session = httpmorph.Session(browser='chrome')
response = session.get('https://example.com')

# HTTP/2 support (httpx-like API)
client = httpmorph.Client(http2=True)
response = client.get('https://www.google.com')
print(response.http_version)  # '2.0'
```

## Browser Profiles

Mimic real browser behavior with pre-configured profiles:

```python
# Use Chrome fingerprint
response = httpmorph.get('https://example.com', browser='chrome')

# Use Firefox fingerprint
session = httpmorph.Session(browser='firefox')
response = session.get('https://example.com')

# Available browsers: chrome, firefox, safari, edge
```

## Advanced Usage

### HTTP/2 Support

httpmorph supports HTTP/2 with an httpx-like API:

```python
# Enable HTTP/2 for a client (default is False)
client = httpmorph.Client(http2=True)
response = client.get('https://www.google.com')
print(response.http_version)  # '2.0'

# Enable HTTP/2 for a session
session = httpmorph.Session(browser='chrome', http2=True)
response = session.get('https://www.google.com')
print(response.http_version)  # '2.0'

# Per-request HTTP/2 override
client = httpmorph.Client(http2=False)  # Default disabled
response = client.get('https://www.google.com', http2=True)  # Enable for this request
```

### Custom Headers

```python
headers = {
    'User-Agent': 'MyApp/1.0',
    'Authorization': 'Bearer token123'
}
response = httpmorph.get('https://api.example.com', headers=headers)
```

### File Uploads

```python
files = {'file': ('report.pdf', open('report.pdf', 'rb'))}
response = httpmorph.post('https://httpbin.org/post', files=files)
```

### Timeout Control

```python
# Single timeout value
response = httpmorph.get('https://example.com', timeout=5)

# Separate connect and read timeouts
response = httpmorph.get('https://example.com', timeout=(3, 10))
```

### SSL Verification

```python
# Disable SSL verification (not recommended for production)
response = httpmorph.get('https://example.com', verify_ssl=False)
```

### Authentication

```python
# Basic authentication
response = httpmorph.get(
    'https://api.example.com',
    auth=('username', 'password')
)
```

### Redirects

```python
# Follow redirects (default behavior)
response = httpmorph.get('https://example.com/redirect')

# Don't follow redirects
response = httpmorph.get(
    'https://example.com/redirect',
    allow_redirects=False
)

# Check redirect history
print(len(response.history))  # Number of redirects
```

### Sessions with Cookies

```python
session = httpmorph.Session()

# Cookies persist across requests
session.get('https://example.com/login')
session.post('https://example.com/form', data={'key': 'value'})

# Access cookies
print(session.cookies)
```

## API Compatibility

httpmorph aims for high compatibility with Python's `requests` library:

| Feature | Status |
|---------|--------|
| GET, POST, PUT, DELETE, HEAD, PATCH, OPTIONS | Supported |
| JSON request/response | Supported |
| Form data & file uploads | Supported |
| Custom headers | Supported |
| Authentication | Supported |
| Cookies & sessions | Supported |
| Redirects with history | Supported |
| Timeout control | Supported |
| SSL verification | Supported |
| Streaming responses | Supported |
| Exception hierarchy | Supported |

## Response Object

```python
response = httpmorph.get('https://httpbin.org/get')

# Status and headers
print(response.status_code)      # 200
print(response.ok)                # True
print(response.reason)            # 'OK'
print(response.headers)           # {'Content-Type': 'application/json', ...}

# Response body
print(response.text)              # Response as string
print(response.body)              # Response as bytes
print(response.json())            # Parse JSON response

# Request info
print(response.url)               # Final URL after redirects
print(response.history)           # List of redirect responses

# Timing
print(response.elapsed)           # Response time
print(response.total_time_us)     # Total time in microseconds

# TLS info
print(response.tls_version)       # TLS version used
print(response.tls_cipher)        # Cipher suite
print(response.ja3_fingerprint)   # JA3 fingerprint
```

## Exception Handling

```python
import httpmorph

try:
    response = httpmorph.get('https://example.com', timeout=5)
    response.raise_for_status()  # Raise exception for 4xx/5xx
except httpmorph.Timeout:
    print("Request timed out")
except httpmorph.ConnectionError:
    print("Failed to connect")
except httpmorph.HTTPError as e:
    print(f"HTTP error: {e.response.status_code}")
except httpmorph.RequestException as e:
    print(f"Request failed: {e}")
```

## Performance

httpmorph is built for speed with a native C implementation and BoringSSL for optimized TLS operations.

**Benchmark vs requests library** (30-second test, macOS Apple Silicon):
- **Throughput:** 49.4% faster on average (1,056 vs 707 req/s)
- **GET requests:** 1,032 req/s vs 709 req/s (1.46x faster)
- **POST requests:** 1,080 req/s vs 705 req/s (1.53x faster)
- **Latency:** 33.1% lower on average (0.95ms vs 1.41ms)

See [detailed benchmark results](benchmarks/results_requests_macos.md) for full metrics including performance charts.

### Performance Features
- Native C implementation with minimal Python overhead
- BoringSSL for optimized TLS operations
- Connection pooling reduces handshake overhead
- HTTP/2 multiplexing for concurrent requests
- Efficient memory management

## Platform Support

| Platform | Status |
|----------|--------|
| Windows | ✅ Fully supported |
| macOS (Intel & Apple Silicon) | ✅ Fully supported |
| Linux (x86_64, ARM64) | ✅ Fully supported |

All platforms use **BoringSSL** (Google's fork of OpenSSL) for consistent TLS behavior and advanced fingerprinting capabilities.

## Building from Source

httpmorph uses **BoringSSL** (built from source) on all platforms for consistent TLS fingerprinting.

### Prerequisites

**Windows:**
```bash
# Install build tools
choco install cmake golang nasm visualstudio2022buildtools -y

# Or install manually:
# - Visual Studio 2019+ (with C++ tools)
# - CMake 3.15+
# - Go 1.18+
# - NASM (for BoringSSL assembly optimizations)
```

**macOS:**
```bash
# Install dependencies
brew install cmake ninja libnghttp2
```

**Linux:**
```bash
# Ubuntu/Debian
sudo apt-get install cmake ninja-build libssl-dev pkg-config autoconf automake libtool

# Fedora/RHEL
sudo dnf install cmake ninja-build openssl-devel pkg-config autoconf automake libtool
```

### Build Steps

```bash
# 1. Clone the repository
git clone https://github.com/arman-bd/httpmorph.git
cd httpmorph

# 2. Build vendor dependencies (BoringSSL, nghttp2, zlib)
./scripts/setup_vendors.sh  # On Windows: bash scripts/setup_vendors.sh

# 3. Build Python extensions
python setup.py build_ext --inplace

# 4. Install in development mode
pip install -e ".[dev]"
```

**Note:** The first build takes 5-10 minutes as it compiles BoringSSL from source. Subsequent builds are much faster (~30 seconds) as the vendor libraries are cached.

## Development

```bash
# Clone and setup (includes building BoringSSL)
git clone https://github.com/arman-bd/httpmorph.git
cd httpmorph
./scripts/setup_vendors.sh

# Install development dependencies
pip install -e ".[dev]"

# Run tests
pytest tests/ -v

# Run with coverage
pytest tests/ --cov=httpmorph --cov-report=html

# Run specific test markers
pytest tests/ -m "not slow"           # Skip slow tests
pytest tests/ -m "not proxy"          # Skip proxy tests (default in CI)
pytest tests/ -m proxy                # Only proxy tests
pytest tests/ -m integration          # Only integration tests
pytest tests/ -m fingerprint          # Only fingerprinting tests
```

## Architecture

httpmorph combines the best of both worlds:

- **C Core**: Low-level HTTP/TLS implementation for maximum performance
- **Python Wrapper**: Clean, Pythonic API with requests compatibility
- **BoringSSL**: Google's fork of OpenSSL, optimized and battle-tested
- **nghttp2**: Standard-compliant HTTP/2 implementation

The library uses Cython to bridge Python and C, providing near-native performance with the ease of Python.

## Contributing

Contributions are welcome! Areas where help is especially appreciated:

- Windows compatibility
- Additional browser profiles
- Performance optimizations
- Documentation improvements
- Bug reports and fixes

Please open an issue or pull request on GitHub.

## Testing

httpmorph has a comprehensive test suite with 270+ tests covering:

- All HTTP methods and parameters
- Redirect handling and history
- Cookie and session management
- Authentication and SSL
- Error handling and timeouts
- Unicode and encoding edge cases
- Thread safety and memory management
- Real-world integration tests

Run the test suite:

```bash
pytest tests/ -v
```

## Acknowledgments

- Built on BoringSSL (Google)
- HTTP/2 support via nghttp2
- Inspired by Python's requests library
- Browser fingerprints based on real browser implementations

## FAQ

**Q: Why another HTTP client?**
A: httpmorph combines the performance of native C with browser fingerprinting capabilities, making it ideal for applications that need both speed and realistic browser behavior.

**Q: Is it production-ready?**
A: No, httpmorph is still in active development and not yet recommended for production use.

**Q: Can I use this as a drop-in replacement for requests?**
A: For most common use cases, yes! We've implemented the most widely-used requests API. Some advanced features may have slight differences.

**Q: How do I report a bug?**
A: Please open an issue on GitHub with a minimal reproduction example and your environment details (OS, Python version, httpmorph version).

## Support

- GitHub Issues: [Report bugs and feature requests](https://github.com/arman-bd/httpmorph/issues)
- Documentation: [Full API documentation](https://httpmorph.readthedocs.io) (coming soon)

---

**httpmorph** - Fast, compatible, and powerful HTTP for Python.
