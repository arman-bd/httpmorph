# httpmorph

> A high-performance HTTP/HTTPS client library for Python, built in C with BoringSSL.

[![Python 3.9+](https://img.shields.io/badge/python-3.9+-blue.svg)](https://www.python.org/downloads/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

## What is httpmorph?

httpmorph is a fast HTTP/HTTPS client library written in C with Python bindings. It focuses on performance and modern TLS support using BoringSSL.

**Current Status:** 🚧 Early development - Basic functionality working, many features in progress.

## Features

### ✅ Implemented
- **Cross-platform support** - Linux, macOS, and Windows
- **TLS/HTTPS** - BoringSSL integration for secure connections
- **Basic HTTP operations** - GET, POST, PUT, DELETE
- **Session management** - Connection reuse and state persistence
- **Timeout support** - Configurable timeouts for requests
- **Response handling** - Status codes, headers, body, timing info
- **Python API** - Clean, requests-like interface

### 🚧 In Development
- HTTP/2 support
- Connection pooling
- Advanced TLS fingerprinting
- io_uring on Linux (currently uses synchronous I/O)
- Async API
- Proxy support

### 📋 Planned
- HTTP/3 support
- Cookie jar
- Redirect handling
- Streaming responses
- Request/response middleware

## Installation

### Requirements
- **Python 3.9+**
- **C compiler** (GCC, Clang, or MSVC)
- **CMake 3.15+**

### Platform-Specific Dependencies

**Linux:**
```bash
sudo apt-get install build-essential cmake ninja-build libssl-dev
```

**macOS:**
```bash
brew install cmake ninja
```

**Windows:**
- Visual Studio Build Tools 2022
- CMake (via Chocolatey: `choco install cmake`)

### Build from Source

```bash
git clone https://github.com/arman-bd/httpmorph.git
cd httpmorph

# Install dependencies
pip install -e ".[dev]"

# Build C extensions
python setup.py build_ext --inplace

# Run tests
pytest tests/ -v
```

## Quick Start

### Simple GET Request

```python
import httpmorph

# Make a GET request
response = httpmorph.get("https://httpbingo.org/get")
print(f"Status: {response.status_code}")
print(f"Body: {response.body[:100]}...")
```

### POST Request

```python
import httpmorph

# POST with JSON
response = httpmorph.post(
    "https://httpbingo.org/post",
    json={"key": "value"}
)
print(response.body)
```

### Using Sessions

```python
import httpmorph

# Create a session for connection reuse
session = httpmorph.Session()

# Make multiple requests
response1 = session.get("https://httpbingo.org/get")
response2 = session.get("https://httpbingo.org/headers")

# Sessions persist cookies and connection state
```

### Custom Headers and Timeouts

```python
import httpmorph

headers = {
    "User-Agent": "MyApp/1.0",
    "Authorization": "Bearer token123"
}

response = httpmorph.get(
    "https://api.example.com/data",
    headers=headers,
    timeout=5.0  # 5 seconds
)
```

### Response Information

```python
response = httpmorph.get("https://httpbingo.org/get")

# Status and headers
print(f"Status: {response.status_code}")
print(f"Headers: {response.headers}")

# Body
print(f"Body length: {len(response.body)} bytes")

# Timing information (in microseconds)
print(f"Connect time: {response.connect_time_us / 1000:.2f}ms")
print(f"TLS handshake: {response.tls_time_us / 1000:.2f}ms")
print(f"Total time: {response.total_time_us / 1000:.2f}ms")

# TLS info
print(f"TLS version: {response.tls_version}")
print(f"Cipher: {response.tls_cipher}")
```

## API Reference

### Module Functions

```python
httpmorph.get(url, headers=None, timeout=30.0)
httpmorph.post(url, data=None, json=None, headers=None, timeout=30.0)
httpmorph.put(url, data=None, json=None, headers=None, timeout=30.0)
httpmorph.delete(url, headers=None, timeout=30.0)
```

### Session Class

```python
session = httpmorph.Session()
session.get(url, headers=None, timeout=30.0)
session.post(url, data=None, json=None, headers=None, timeout=30.0)
session.put(url, data=None, json=None, headers=None, timeout=30.0)
session.delete(url, headers=None, timeout=30.0)
```

### Response Object

```python
response.status_code      # HTTP status code (int)
response.headers          # Response headers (dict)
response.body             # Response body (bytes)
response.text             # Response body as string
response.connect_time_us  # Connection time (microseconds)
response.tls_time_us      # TLS handshake time (microseconds)
response.total_time_us    # Total request time (microseconds)
response.tls_version      # TLS version string
response.tls_cipher       # Cipher suite used
response.ja3_fingerprint  # JA3 fingerprint (if available)
```

## Architecture

```
httpmorph/
├── Python API (httpmorph/__init__.py)
├── Cython bindings (bindings/_httpmorph.pyx)
└── C Core
    ├── httpmorph.c        - Main client logic (70KB)
    ├── io_engine.c        - I/O abstraction (7.5KB)
    ├── http2_client.c     - HTTP/2 support (stub)
    └── TLS via BoringSSL
```

## Performance

httpmorph is built for performance with:
- **C implementation** - Direct syscalls, minimal overhead
- **BoringSSL** - Google's optimized TLS library
- **Connection reuse** - Sessions maintain persistent connections
- **Zero-copy where possible** - Efficient memory handling

**Note:** io_uring support (Linux) is planned but not yet implemented. Currently uses synchronous I/O on all platforms.

## Platform Support

| Platform | Status | I/O Engine | TLS Library |
|----------|--------|------------|-------------|
| Linux    | ✅ Supported | Synchronous (io_uring planned) | BoringSSL |
| macOS    | ✅ Supported | BSD sockets | BoringSSL |
| Windows  | ✅ Supported | Winsock2 | BoringSSL |

## Development

### Running Tests

```bash
# All tests
pytest tests/ -v

# With coverage
pytest tests/ -v --cov=httpmorph --cov-report=html

# Specific test file
pytest tests/test_basic.py -v
```

### Code Quality

```bash
# Lint
ruff check src/ tests/

# Format
ruff format src/ tests/

# Type check
mypy src/
```

### Pre-commit Hook

Install the pre-commit hook to run linting before commits:

```bash
cp hooks/pre-commit .git/hooks/pre-commit
chmod +x .git/hooks/pre-commit
```

This runs `ruff check` before allowing commits.

## CI/CD

GitHub Actions workflows automatically:
- **Test** - Run tests on Linux, macOS, Windows with Python 3.9-3.12
- **Benchmark** - Track performance on main branch
- **Release** - Build wheels and publish to PyPI on version tags

## Project Status

### What Works Now
- ✅ Basic HTTP/HTTPS requests (GET, POST, PUT, DELETE)
- ✅ Sessions with connection reuse
- ✅ Custom headers and timeouts
- ✅ Response parsing (status, headers, body)
- ✅ TLS via BoringSSL
- ✅ Cross-platform (Linux, macOS, Windows)
- ✅ **Automatic redirect following**
- ✅ **Cookie jar with persistence**
- ✅ **Proxy support** (parameter accepted)
- ✅ 181 passing tests

### Known Limitations
- ⚠️ **No io_uring** - Code exists but not enabled (uses synchronous I/O on all platforms)
- ⚠️ **No HTTP/2** - Only stub implementation (HTTP/1.1 only)
- ⚠️ No async API
- ⚠️ No streaming responses
- ⚠️ No manual redirect control

**Note:** io_uring code framework is implemented but requires Linux 5.1+ and is not yet enabled in builds.

### Roadmap

**v0.2.0** (Next)
- HTTP/2 support
- Improved error handling
- Manual redirect control
- Streaming responses

**v0.3.0**
- io_uring on Linux
- Async API
- Advanced proxy features (SOCKS5, auth)

**v1.0.0**
- HTTP/3 support
- Production ready
- Full documentation

## Why httpmorph?

- **Native C performance** - Not a wrapper around curl or other libraries
- **Modern TLS** - BoringSSL for up-to-date security
- **Cross-platform** - One codebase, three platforms
- **Simple API** - Familiar requests-like interface
- **Active development** - Regular updates and improvements

**Missing dependencies:**
```bash
# Linux
sudo apt-get install build-essential cmake libssl-dev ninja-build

# macOS
brew install cmake ninja

# Windows
choco install cmake visualstudio2022buildtools
```

**Clean rebuild:**
```bash
make clean-all
python setup.py build_ext --inplace
```

### Import Errors

**"C extension not available":**
```bash
# Rebuild extension
python setup.py build_ext --inplace

# Verify
python -c "import httpmorph; print(httpmorph.version())"
```

### Runtime Errors

**Connection failures:**
- Check firewall settings
- Verify URL is accessible
- Try increasing timeout

**SSL errors:**
- Ensure target server has valid certificate
- Check system time is correct

## Contributing

Contributions welcome! Areas where help is needed:
- io_uring implementation
- HTTP/2 support
- Connection pooling
- Test coverage
- Documentation
- Windows testing

## License

MIT License - see [LICENSE](LICENSE) for details.

## Links

- **GitHub**: https://github.com/arman-bd/httpmorph
- **Issues**: https://github.com/arman-bd/httpmorph/issues

---

**httpmorph** - Fast HTTP client built in C with BoringSSL
