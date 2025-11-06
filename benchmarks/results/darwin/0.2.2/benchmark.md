# httpmorph Benchmark Results

**Version:** 0.2.2 | **Generated:** 2025-11-06

## System Information

| Property | Value |
|----------|-------|
| **OS** | Darwin (macOS-15.6-arm64-arm-64bit-Mach-O) |
| **Processor** | arm |
| **CPU Cores** | 10 |
| **Memory** | 16.0 GB |
| **Python** | 3.14.0 (CPython) |

## Test Configuration

- **Sequential Requests:** 25 (warmup: 5)
- **Concurrent Requests:** 25 (workers: 5)

## Library Versions

| Library | Version | Status |
|---------|---------|--------|
| **httpmorph** | `0.2.2` | Installed |
| **requests** | `2.32.5` | Installed |
| **httpx** | `0.28.1` | Installed |
| **aiohttp** | `3.13.1` | Installed |
| **urllib3** | `2.5.0` | Installed |
| **urllib** | `built-in (Python 3.14.0)` | Installed |
| **pycurl** | `PycURL/7.45.7 libcurl/8.16.0-DEV OpenSSL/3.5.2 zlib/1.3.1 brotli/1.1.0 libssh2/1.11.1_DEV nghttp2/1.67.0` | Installed |
| **curl_cffi** | `0.13.0` | Installed |

## Sequential Tests (Lower is Better)

Mean response time in milliseconds

| Library | Local HTTP | Proxy HTTP | Proxy HTTP2 | Proxy HTTPs | Remote HTTP | Remote HTTP2 | Remote HTTPs |
|---------|--------:|--------:|--------:|--------:|--------:|--------:|--------:|
| **curl_cffi** | 1.25ms | 1212.26ms | 1646.60ms | 2131.77ms | 64.09ms | 100.49ms | 99.58ms |
| **httpmorph** | 1.34ms | 1260.38ms | 1922.90ms | 2272.14ms | 61.63ms | 103.33ms | 99.36ms |
| **httpx** | 5.54ms | 411.45ms | 433.30ms | 404.70ms | 62.93ms | 101.15ms | 97.87ms |
| **pycurl** | 0.40ms | 1777.37ms | 2499.49ms | 2265.31ms | 65.92ms | 97.96ms | 103.89ms |
| **requests** | 0.76ms | 351.83ms | N/A | 437.27ms | 26.97ms | N/A | 31.75ms |
| **urllib** | 7.94ms | 1624.63ms | N/A | 2161.49ms | 63.97ms | N/A | 126.57ms |
| **urllib3** | 0.61ms | 345.31ms | N/A | 429.76ms | 30.93ms | N/A | 27.19ms |

**Winners (Sequential):**
- Local HTTP: **pycurl** (0.40ms)
- Proxy HTTP: **urllib3** (345.31ms)
- Proxy HTTP2: **httpx** (433.30ms)
- Proxy HTTPs: **httpx** (404.70ms)
- Remote HTTP: **requests** (26.97ms)
- Remote HTTP2: **pycurl** (97.96ms)
- Remote HTTPs: **urllib3** (27.19ms)

## Concurrent Tests (Higher is Better)

Throughput in requests per second

| Library | Local HTTP | Proxy HTTP | Proxy HTTP2 | Proxy HTTPs | Remote HTTP | Remote HTTP2 | Remote HTTPs |
|---------|--------:|--------:|--------:|--------:|--------:|--------:|--------:|
| **curl_cffi** | 253.97 | 2.86 | 2.16 | 1.90 | 74.04 | 47.88 | 47.67 |
| **httpmorph** | 926.81 | 2.04 | 1.62 | 1.92 | 65.03 | 51.93 | 50.31 |
| **httpx** | 763.12 | 4.30 | 7.23 | 8.71 | 72.56 | 47.69 | 49.16 |
| **pycurl** | 2156.50 | 4.99 | 1.75 | 1.31 | 68.81 | 50.28 | 44.22 |
| **requests** | 757.97 | 4.26 | N/A | 3.51 | 119.46 | N/A | 93.74 |
| **urllib** | 259.93 | 2.65 | N/A | 2.11 | 69.41 | N/A | 45.28 |
| **urllib3** | 1262.19 | 4.30 | N/A | 3.79 | 111.02 | N/A | 104.16 |

**Winners (Concurrent):**
- Local HTTP: **pycurl** (2156.50 req/s)
- Proxy HTTP: **pycurl** (4.99 req/s)
- Proxy HTTP2: **httpx** (7.23 req/s)
- Proxy HTTPs: **httpx** (8.71 req/s)
- Remote HTTP: **requests** (119.46 req/s)
- Remote HTTP2: **httpmorph** (51.93 req/s)
- Remote HTTPs: **urllib3** (104.16 req/s)

## Async Tests (Higher is Better)

Throughput in requests per second

| Library | Local HTTP | Proxy HTTP | Proxy HTTP2 | Proxy HTTPs | Remote HTTP | Remote HTTP2 | Remote HTTPs |
|---------|--------:|--------:|--------:|--------:|--------:|--------:|--------:|
| **aiohttp** | 133.56 | 4.27 | N/A | 2.33 | 247.87 | N/A | 176.24 |
| **httpmorph** | 154.43 | 65.93 | 3.72 | 3.72 | 246.05 | 154.19 | 165.44 |
| **httpx** | 156.96 | 4.28 | 11.19 | 3.60 | 220.18 | 141.84 | 134.49 |

**Winners (Async):**
- Local HTTP: **httpx** (156.96 req/s)
- Proxy HTTP: **httpmorph** (65.93 req/s)
- Proxy HTTP2: **httpx** (11.19 req/s)
- Proxy HTTPs: **httpmorph** (3.72 req/s)
- Remote HTTP: **aiohttp** (247.87 req/s)
- Remote HTTP2: **httpmorph** (154.19 req/s)
- Remote HTTPs: **aiohttp** (176.24 req/s)

## Overall Performance Summary

### Sequential Tests: httpmorph vs requests Speedup

| Test | httpmorph | requests | Speedup |
|------|----------:|---------:|--------:|
| Local HTTP | 1.34ms | 0.76ms | 0.57x slower |
| Proxy HTTP | 1260.38ms | 351.83ms | 0.28x slower |
| Proxy HTTPs | 2272.14ms | 437.27ms | 0.19x slower |
| Remote HTTP | 61.63ms | 26.97ms | 0.44x slower |
| Remote HTTPs | 99.36ms | 31.75ms | 0.32x slower |

### Concurrent Tests: httpmorph vs requests Speedup

| Test | httpmorph | requests | Speedup |
|------|----------:|---------:|--------:|
| Local HTTP | 926.81 req/s | 757.97 req/s | **1.22x** faster |
| Proxy HTTP | 2.04 req/s | 4.26 req/s | 0.48x slower |
| Proxy HTTPs | 1.92 req/s | 3.51 req/s | 0.55x slower |
| Remote HTTP | 65.03 req/s | 119.46 req/s | 0.54x slower |
| Remote HTTPs | 50.31 req/s | 93.74 req/s | 0.54x slower |

### Async Tests: httpmorph vs httpx Speedup

| Test | httpmorph | httpx | Speedup |
|------|----------:|------:|--------:|
| Local HTTP | 154.43 req/s | 156.96 req/s | 0.98x slower |
| Proxy HTTP | 65.93 req/s | 4.28 req/s | **15.40x** faster |
| Proxy HTTP2 | 3.72 req/s | 11.19 req/s | 0.33x slower |
| Proxy HTTPs | 3.72 req/s | 3.60 req/s | **1.03x** faster |
| Remote HTTP | 246.05 req/s | 220.18 req/s | **1.12x** faster |
| Remote HTTP2 | 154.19 req/s | 141.84 req/s | **1.09x** faster |
| Remote HTTPs | 165.44 req/s | 134.49 req/s | **1.23x** faster |

---
*Generated by httpmorph benchmark suite*
