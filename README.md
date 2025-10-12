# httpmorph

[![codecov](https://codecov.io/gh/arman-bd/httpmorph/graph/badge.svg?token=D7BCC52PQN)](https://codecov.io/gh/arman-bd/httpmorph)

A Python HTTP client focused on mimicking browser fingerprints.

**⚠️ Work in Progress** - This library is in early development. Features and API may change.

## What is httpmorph?

httpmorph is a drop-in replacement for Python's `requests` library that uses a custom C implementation with BoringSSL instead of Python's standard HTTP stack. The primary goal is **mimicking real browser TLS/HTTP fingerprints**, not performance.

If you need raw speed, use `httpx` or `aiohttp`. If you need your Python script to look like Chrome, Firefox, or Safari from a fingerprinting perspective, use httpmorph.

## Why httpmorph?

Many websites use TLS/HTTP fingerprinting to detect automated clients. Python's `requests` library has a distinctive fingerprint:

- Uses OpenSSL with Python's default configuration
- Specific cipher suite ordering that doesn't match browsers
- Missing browser-specific TLS extensions (like GREASE)
- Different HTTP/2 settings than real browsers

httpmorph addresses this by using BoringSSL (same as Chrome) and implementing exact TLS/HTTP parameters from real browsers.

## Installation

```bash
pip install httpmorph
```

**Requirements:**
- Python 3.8+
- Supported platforms: Linux, macOS, Windows

## Quick Start

```python
import httpmorph

# Simple GET request (uses Chrome fingerprint by default)
response = httpmorph.get('https://httpbin.org/get')
print(response.text)

# Create a session with a specific browser profile
session = httpmorph.Session(browser='firefox')
response = session.get('https://httpbin.org/headers')
print(response.json())

# Session automatically handles cookies
response = session.post('https://httpbin.org/post', json={'key': 'value'})
```

## API Compatibility

httpmorph provides a `requests`-compatible API:

```python
# Module-level convenience functions
httpmorph.get(url, **kwargs)
httpmorph.post(url, data=None, json=None, **kwargs)
httpmorph.put(url, data=None, **kwargs)
httpmorph.delete(url, **kwargs)
httpmorph.head(url, **kwargs)
httpmorph.patch(url, data=None, **kwargs)
httpmorph.options(url, **kwargs)

# Session object
session = httpmorph.Session(browser='chrome')
session.get(url)
session.post(url, json={...})

# Response object
response.status_code
response.headers
response.text
response.content  # bytes
response.json()
response.raise_for_status()
response.elapsed  # timedelta
```

## Browser Profiles

Available browser profiles:

- `chrome` - Chrome 131 (default)
- `firefox` - Firefox 122
- `safari` - Safari 17
- `edge` - Edge 122

Each profile includes:
- Accurate User-Agent header
- TLS cipher suites in correct order
- TLS extensions (including GREASE for Chrome/Edge)
- Elliptic curves
- Signature algorithms
- HTTP/2 SETTINGS frame parameters
- JA3 fingerprint characteristics

```python
# Use a specific browser profile
session = httpmorph.Session(browser='firefox')
response = session.get('https://example.com')

# View TLS information
print(f"TLS Version: {response.tls_version}")
print(f"Cipher: {response.tls_cipher}")
print(f"JA3: {response.ja3_fingerprint}")
```

## Response Timing

Responses include detailed timing information:

```python
response = httpmorph.get('https://example.com')

# Timing in microseconds
print(f"Connect: {response.connect_time_us / 1000:.2f}ms")
print(f"TLS handshake: {response.tls_time_us / 1000:.2f}ms")
print(f"First byte: {response.first_byte_time_us / 1000:.2f}ms")
print(f"Total: {response.total_time_us / 1000:.2f}ms")

# requests-compatible
print(f"Elapsed: {response.elapsed.total_seconds():.3f}s")
```

## Common Use Cases

### Custom Headers

```python
headers = {'Authorization': 'Bearer token123'}
response = httpmorph.get('https://api.example.com', headers=headers)
```

### File Uploads

```python
files = {'file': ('report.pdf', open('report.pdf', 'rb'))}
response = httpmorph.post('https://httpbin.org/post', files=files)
```

### Timeout Control

```python
# Single timeout value (seconds)
response = httpmorph.get('https://example.com', timeout=5)

# Tuple: (connect_timeout, read_timeout)
response = httpmorph.get('https://example.com', timeout=(3, 10))
```

### Authentication

```python
# Basic authentication
response = httpmorph.get(
    'https://api.example.com',
    auth=('username', 'password')
)
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

### Error Handling

```python
try:
    response = httpmorph.get('https://example.com', timeout=5)
    response.raise_for_status()
except httpmorph.Timeout:
    print("Request timed out")
except httpmorph.ConnectionError:
    print("Failed to connect")
except httpmorph.HTTPError as e:
    print(f"HTTP error: {e.response.status_code}")
```

## Current Limitations

This library is a work in progress. Known limitations:

- No proxy support
- No SSL verification configuration
- No connection pooling
- No streaming requests/responses
- Limited cookie handling (basic support)
- No custom auth handlers
- HTTP/2 support is partial

These may be addressed in future releases.

## Building from Source

```bash
# 1. Clone the repository
git clone https://github.com/arman-bd/httpmorph
cd httpmorph

# 2. Build vendor dependencies (BoringSSL, nghttp2)
#    This takes 5-10 minutes on first build
bash scripts/setup_vendors.sh

# 3. Install in development mode
pip install -e ".[dev]"

# 4. Run tests
pytest tests/ -v
```

### Platform-Specific Prerequisites

**macOS:**
```bash
brew install cmake ninja go
```

**Linux (Ubuntu/Debian):**
```bash
sudo apt-get install cmake ninja-build golang pkg-config autoconf automake libtool
```

**Windows:**
```bash
choco install cmake golang -y
# Also requires Visual Studio 2019+ with C++ tools
```

## Architecture

httpmorph has three layers:

1. **C Core** (`src/core/`) - Socket handling, TLS with BoringSSL, HTTP parsing
2. **Cython Bindings** (`src/bindings/`) - Python interface to C code
3. **Python Wrapper** (`src/httpmorph/`) - `requests`-compatible API

Browser TLS profiles are defined in `src/tls/browser_profiles.c`.

## License

MIT License - see [LICENSE](LICENSE) file for details.

## Contributing

This project is in early development. Contributions welcome, but please note the API is still evolving.

## Disclaimer

This tool is for legitimate testing and development purposes only. Users are responsible for complying with website terms of service and applicable laws.
