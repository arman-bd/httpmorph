# httpmorph Benchmark Results

**Version:** 0.2.4 | **Generated:** 2025-11-07

## System Information

| Property | Value |
|----------|-------|
| **OS** | Darwin (macOS-15.6-arm64-arm-64bit) |
| **Processor** | arm |
| **CPU Cores** | 10 |
| **Memory** | 16.0 GB |
| **Python** | 3.11.5 (CPython) |

## Test Configuration

- **Sequential Requests:** 25 (warmup: 5)
- **Concurrent Requests:** 25 (workers: 5)

## Library Versions

| Library | Version | Status |
|---------|---------|--------|
| **httpmorph** | `0.2.4` | Installed |
| **requests** | `2.31.0` | Installed |
| **httpx** | `0.27.2` | Installed |
| **aiohttp** | `3.8.5` | Installed |
| **urllib3** | `1.26.16` | Installed |
| **urllib** | `built-in (Python 3.11.5)` | Installed |
| **pycurl** | `PycURL/7.45.2 libcurl/8.7.1 (SecureTransport) LibreSSL/3.3.6 zlib/1.2.12 nghttp2/1.64.0` | Installed |
| **curl_cffi** | `0.13.0` | Installed |

## Sequential Tests (Lower is Better)

Mean response time in milliseconds

| Library | Local HTTP | Proxy HTTP | Proxy HTTP2 | Proxy HTTPs | Remote HTTP | Remote HTTP2 | Remote HTTPs |
|---------|--------:|--------:|--------:|--------:|--------:|--------:|--------:|
| **curl_cffi** | 0.43ms | ERROR | ERROR | 1576.17ms | 65.70ms | 101.59ms | 94.96ms |
| **httpmorph** | 0.31ms | 891.02ms | 1620.74ms | 1679.02ms | 62.20ms | 98.39ms | 97.47ms |
| **httpx** | 1.14ms | 596.42ms | 338.85ms | 367.00ms | 66.42ms | 96.58ms | 102.12ms |
| **pycurl** | 0.43ms | 1347.29ms | 1700.27ms | ERROR | 62.17ms | 103.57ms | 100.06ms |
| **requests** | 0.94ms | 349.35ms | N/A | 338.11ms | 31.50ms | N/A | 26.71ms |
| **urllib** | 15.83ms | 1094.31ms | N/A | 1785.94ms | 65.10ms | N/A | 120.19ms |
| **urllib3** | 0.36ms | 397.95ms | N/A | 387.72ms | 30.93ms | N/A | 36.31ms |

**Winners (Sequential):**
- Local HTTP: **httpmorph** (0.31ms)
- Proxy HTTP: **requests** (349.35ms)
- Proxy HTTP2: **httpx** (338.85ms)
- Proxy HTTPs: **requests** (338.11ms)
- Remote HTTP: **urllib3** (30.93ms)
- Remote HTTP2: **httpx** (96.58ms)
- Remote HTTPs: **requests** (26.71ms)

## Concurrent Tests (Higher is Better)

Throughput in requests per second

| Library | Local HTTP | Proxy HTTP | Proxy HTTP2 | Proxy HTTPs | Remote HTTP | Remote HTTP2 | Remote HTTPs |
|---------|--------:|--------:|--------:|--------:|--------:|--------:|--------:|
| **curl_cffi** | 1645.08 | 2.31 | 2.73 | ERROR | 68.06 | 46.08 | 48.57 |
| **httpmorph** | 6769.95 | 5.43 | 3.24 | 2.31 | 76.03 | 50.09 | 51.35 |
| **httpx** | 803.58 | 10.68 | ERROR | 3.84 | 75.23 | 49.41 | 48.82 |
| **pycurl** | 2757.38 | 2.35 | 2.58 | 2.39 | 70.46 | 46.50 | 36.88 |
| **requests** | 1040.44 | 10.51 | N/A | 6.45 | 126.58 | N/A | 57.34 |
| **urllib** | 25.74 | 2.73 | N/A | 2.25 | 72.05 | N/A | 28.49 |
| **urllib3** | 1422.15 | 5.35 | N/A | 7.94 | 103.52 | N/A | 57.53 |

**Winners (Concurrent):**
- Local HTTP: **httpmorph** (6769.95 req/s)
- Proxy HTTP: **httpx** (10.68 req/s)
- Proxy HTTP2: **httpmorph** (3.24 req/s)
- Proxy HTTPs: **urllib3** (7.94 req/s)
- Remote HTTP: **requests** (126.58 req/s)
- Remote HTTP2: **httpmorph** (50.09 req/s)
- Remote HTTPs: **urllib3** (57.53 req/s)

## Async Tests (Higher is Better)

Throughput in requests per second

| Library | Local HTTP | Proxy HTTP | Proxy HTTP2 | Proxy HTTPs | Remote HTTP | Remote HTTP2 | Remote HTTPs |
|---------|--------:|--------:|--------:|--------:|--------:|--------:|--------:|
| **aiohttp** | 165.03 | 4.39 | N/A | 10.76 | 254.00 | N/A | 115.49 |
| **httpmorph** | 193.16 | 66.86 | 3.78 | 3.79 | 241.08 | 170.36 | 165.16 |
| **httpx** | 158.09 | 4.29 | 4.01 | 3.99 | 221.29 | 137.07 | 165.55 |

**Winners (Async):**
- Local HTTP: **httpmorph** (193.16 req/s)
- Proxy HTTP: **httpmorph** (66.86 req/s)
- Proxy HTTP2: **httpx** (4.01 req/s)
- Proxy HTTPs: **aiohttp** (10.76 req/s)
- Remote HTTP: **aiohttp** (254.00 req/s)
- Remote HTTP2: **httpmorph** (170.36 req/s)
- Remote HTTPs: **httpx** (165.55 req/s)

## Overall Performance Summary

### Sequential Tests: httpmorph vs requests Speedup

| Test | httpmorph | requests | Speedup |
|------|----------:|---------:|--------:|
| Local HTTP | 0.31ms | 0.94ms | **3.01x** faster |
| Proxy HTTP | 891.02ms | 349.35ms | 0.39x slower |
| Proxy HTTPs | 1679.02ms | 338.11ms | 0.20x slower |
| Remote HTTP | 62.20ms | 31.50ms | 0.51x slower |
| Remote HTTPs | 97.47ms | 26.71ms | 0.27x slower |

### Concurrent Tests: httpmorph vs requests Speedup

| Test | httpmorph | requests | Speedup |
|------|----------:|---------:|--------:|
| Local HTTP | 6769.95 req/s | 1040.44 req/s | **6.51x** faster |
| Proxy HTTP | 5.43 req/s | 10.51 req/s | 0.52x slower |
| Proxy HTTPs | 2.31 req/s | 6.45 req/s | 0.36x slower |
| Remote HTTP | 76.03 req/s | 126.58 req/s | 0.60x slower |
| Remote HTTPs | 51.35 req/s | 57.34 req/s | 0.90x slower |

### Async Tests: httpmorph vs httpx Speedup

| Test | httpmorph | httpx | Speedup |
|------|----------:|------:|--------:|
| Local HTTP | 193.16 req/s | 158.09 req/s | **1.22x** faster |
| Proxy HTTP | 66.86 req/s | 4.29 req/s | **15.60x** faster |
| Proxy HTTP2 | 3.78 req/s | 4.01 req/s | 0.94x slower |
| Proxy HTTPs | 3.79 req/s | 3.99 req/s | 0.95x slower |
| Remote HTTP | 241.08 req/s | 221.29 req/s | **1.09x** faster |
| Remote HTTP2 | 170.36 req/s | 137.07 req/s | **1.24x** faster |
| Remote HTTPs | 165.16 req/s | 165.55 req/s | 1.00x slower |

---
*Generated by httpmorph benchmark suite*
