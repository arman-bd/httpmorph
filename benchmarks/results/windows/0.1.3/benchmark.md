# httpmorph Benchmark Results

**Version:** 0.1.3 | **Generated:** 2025-11-03

## System Information

| Property | Value |
|----------|-------|
| **OS** | Windows (Windows-10-10.0.20348-SP0) |
| **Processor** | AMD64 Family 23 Model 1 Stepping 2, AuthenticAMD |
| **CPU Cores** | 3 |
| **Python** | 3.11.9 (CPython) |

## Test Configuration

- **Sequential Requests:** 25 (warmup: 5)
- **Concurrent Requests:** 25 (workers: 10)

## Library Versions

| Library | Version | Status |
|---------|---------|--------|
| **httpmorph** | `0.1.3` | Installed |
| **requests** | `2.32.5` | Installed |
| **httpx** | `0.28.1` | Installed |
| **aiohttp** | `3.13.2` | Installed |
| **urllib3** | `2.5.0` | Installed |
| **urllib** | `built-in (Python 3.11.9)` | Installed |
| **pycurl** | `PycURL/"7.45.7" libcurl/8.15.0-DEV (OpenSSL/3.5.2) Schannel zlib/1.3.1 brotli/1.1.0 libssh2/1.11.1_DEV nghttp2/1.67.0` | Installed |
| **curl_cffi** | `0.13.0` | Installed |

## Sequential Tests (Lower is Better)

Mean response time in milliseconds

| Library | Local HTTP | Proxy HTTP | Proxy HTTP2 | Proxy HTTPs | Remote HTTP | Remote HTTP2 | Remote HTTPs |
|---------|--------:|--------:|--------:|--------:|--------:|--------:|--------:|
| **curl_cffi** | 0.99ms | 961.02ms | 2344.36ms | 1518.78ms | 1.93ms | 2.91ms | 3.33ms |
| **httpmorph** | 1.00ms | 1507.04ms | 2031.50ms | 2505.46ms | 3.41ms | 8.29ms | 4.45ms |
| **httpx** | 2.14ms | 402.21ms | 339.81ms | 326.76ms | 6.97ms | 9.94ms | 10.61ms |
| **pycurl** | 0.47ms | 690.92ms | 1774.27ms | 1770.54ms | 1.11ms | 4.54ms | 4.47ms |
| **requests** | 1.82ms | 353.12ms | N/A | 360.24ms | 1.33ms | N/A | 1.59ms |
| **urllib** | 15.69ms | 1086.98ms | N/A | 1561.29ms | 1.50ms | N/A | 23.45ms |
| **urllib3** | 0.81ms | 327.29ms | N/A | 338.20ms | 0.43ms | N/A | 0.50ms |

**Winners (Sequential):**
- Local HTTP: **pycurl** (0.47ms)
- Proxy HTTP: **urllib3** (327.29ms)
- Proxy HTTP2: **httpx** (339.81ms)
- Proxy HTTPs: **httpx** (326.76ms)
- Remote HTTP: **urllib3** (0.43ms)
- Remote HTTP2: **curl_cffi** (2.91ms)
- Remote HTTPs: **urllib3** (0.50ms)

## Concurrent Tests (Higher is Better)

Throughput in requests per second

| Library | Local HTTP | Proxy HTTP | Proxy HTTP2 | Proxy HTTPs | Remote HTTP | Remote HTTP2 | Remote HTTPs |
|---------|--------:|--------:|--------:|--------:|--------:|--------:|--------:|
| **curl_cffi** | 47.00 | 3.99 | 2.88 | 3.43 | 823.02 | 703.15 | 785.86 |
| **httpmorph** | 46.94 | 4.00 | 2.19 | 2.78 | 787.51 | 438.24 | 703.78 |
| **httpx** | 46.38 | 18.60 | ERROR | 4.01 | 589.42 | 331.39 | 290.30 |
| **pycurl** | 47.30 | 3.81 | 2.82 | 2.89 | 548.84 | 262.17 | 300.59 |
| **requests** | 45.93 | 4.38 | N/A | 3.86 | 646.75 | N/A | 151.49 |
| **urllib** | 33.14 | 3.77 | N/A | 3.21 | 1148.80 | N/A | 63.93 |
| **urllib3** | 44.66 | 4.41 | N/A | 3.91 | 1403.42 | N/A | 108.96 |

**Winners (Concurrent):**
- Local HTTP: **pycurl** (47.30 req/s)
- Proxy HTTP: **httpx** (18.60 req/s)
- Proxy HTTP2: **curl_cffi** (2.88 req/s)
- Proxy HTTPs: **httpx** (4.01 req/s)
- Remote HTTP: **urllib3** (1403.42 req/s)
- Remote HTTP2: **curl_cffi** (703.15 req/s)
- Remote HTTPs: **curl_cffi** (785.86 req/s)

## Async Tests (Higher is Better)

Throughput in requests per second

| Library | Local HTTP | Proxy HTTP | Proxy HTTP2 | Proxy HTTPs | Remote HTTP | Remote HTTP2 | Remote HTTPs |
|---------|--------:|--------:|--------:|--------:|--------:|--------:|--------:|
| **aiohttp** | 46.98 | 4.38 | N/A | 3.77 | 999.64 | N/A | 335.96 |
| **httpmorph** | 22.52 | 4.09 | 3.84 | 3.83 | 292.00 | 184.65 | 199.90 |
| **httpx** | 42.79 | 4.37 | 3.87 | 3.88 | 352.38 | 148.01 | 244.85 |

**Winners (Async):**
- Local HTTP: **aiohttp** (46.98 req/s)
- Proxy HTTP: **aiohttp** (4.38 req/s)
- Proxy HTTP2: **httpx** (3.87 req/s)
- Proxy HTTPs: **httpx** (3.88 req/s)
- Remote HTTP: **aiohttp** (999.64 req/s)
- Remote HTTP2: **httpmorph** (184.65 req/s)
- Remote HTTPs: **aiohttp** (335.96 req/s)

## Overall Performance Summary

### Sequential Tests: httpmorph vs requests Speedup

| Test | httpmorph | requests | Speedup |
|------|----------:|---------:|--------:|
| Local HTTP | 1.00ms | 1.82ms | **1.83x** faster |
| Proxy HTTP | 1507.04ms | 353.12ms | 0.23x slower |
| Proxy HTTPs | 2505.46ms | 360.24ms | 0.14x slower |
| Remote HTTP | 3.41ms | 1.33ms | 0.39x slower |
| Remote HTTPs | 4.45ms | 1.59ms | 0.36x slower |

### Concurrent Tests: httpmorph vs requests Speedup

| Test | httpmorph | requests | Speedup |
|------|----------:|---------:|--------:|
| Local HTTP | 46.94 req/s | 45.93 req/s | **1.02x** faster |
| Proxy HTTP | 4.00 req/s | 4.38 req/s | 0.91x slower |
| Proxy HTTPs | 2.78 req/s | 3.86 req/s | 0.72x slower |
| Remote HTTP | 787.51 req/s | 646.75 req/s | **1.22x** faster |
| Remote HTTPs | 703.78 req/s | 151.49 req/s | **4.65x** faster |

### Async Tests: httpmorph vs httpx Speedup

| Test | httpmorph | httpx | Speedup |
|------|----------:|------:|--------:|
| Local HTTP | 22.52 req/s | 42.79 req/s | 0.53x slower |
| Proxy HTTP | 4.09 req/s | 4.37 req/s | 0.94x slower |
| Proxy HTTP2 | 3.84 req/s | 3.87 req/s | 0.99x slower |
| Proxy HTTPs | 3.83 req/s | 3.88 req/s | 0.99x slower |
| Remote HTTP | 292.00 req/s | 352.38 req/s | 0.83x slower |
| Remote HTTP2 | 184.65 req/s | 148.01 req/s | **1.25x** faster |
| Remote HTTPs | 199.90 req/s | 244.85 req/s | 0.82x slower |

---
*Generated by httpmorph benchmark suite*
