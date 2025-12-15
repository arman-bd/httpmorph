# httpmorph Benchmark Results

**Version:** 0.2.9 | **Generated:** 2025-12-15

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
| **httpmorph** | `0.2.9` | Installed |
| **requests** | `2.31.0` | Installed |
| **httpx** | `0.28.1` | Installed |
| **aiohttp** | `3.13.2` | Installed |
| **urllib3** | `1.26.16` | Installed |
| **urllib** | `built-in (Python 3.11.5)` | Installed |
| **pycurl** | `PycURL/7.45.2 libcurl/8.7.1 (SecureTransport) LibreSSL/3.3.6 zlib/1.2.12 nghttp2/1.64.0` | Installed |
| **curl_cffi** | `0.13.0` | Installed |

## Sequential Tests (Lower is Better)

Mean response time in milliseconds

| Library | Local HTTP | Proxy HTTP | Proxy HTTP2 | Proxy HTTPs | Remote HTTP | Remote HTTP2 | Remote HTTPs |
|---------|--------:|--------:|--------:|--------:|--------:|--------:|--------:|
| **curl_cffi** | 0.56ms | 1165.63ms | ERROR | 1866.44ms | 380.18ms | 582.56ms | 571.34ms |
| **httpmorph** | 0.14ms | 1170.21ms | 1782.81ms | 2048.53ms | 189.68ms | 187.65ms | 188.44ms |
| **httpx** | 0.61ms | 490.34ms | 320.46ms | 343.61ms | 381.17ms | 571.08ms | 571.55ms |
| **pycurl** | 0.29ms | 1535.44ms | 3263.55ms | ERROR | 380.55ms | 580.41ms | 583.38ms |
| **requests** | 1.16ms | 374.35ms | N/A | 467.73ms | 194.14ms | N/A | 189.35ms |
| **urllib** | 14.93ms | 1240.06ms | N/A | 2550.24ms | 380.10ms | N/A | 612.15ms |
| **urllib3** | 0.56ms | 339.93ms | N/A | 483.14ms | 202.21ms | N/A | 202.19ms |

**Winners (Sequential):**
- Local HTTP: **httpmorph** (0.14ms)
- Proxy HTTP: **urllib3** (339.93ms)
- Proxy HTTP2: **httpx** (320.46ms)
- Proxy HTTPs: **httpx** (343.61ms)
- Remote HTTP: **httpmorph** (189.68ms)
- Remote HTTP2: **httpmorph** (187.65ms)
- Remote HTTPs: **httpmorph** (188.44ms)

## Concurrent Tests (Higher is Better)

Throughput in requests per second

| Library | Local HTTP | Proxy HTTP | Proxy HTTP2 | Proxy HTTPs | Remote HTTP | Remote HTTP2 | Remote HTTPs |
|---------|--------:|--------:|--------:|--------:|--------:|--------:|--------:|
| **curl_cffi** | 1419.48 | 3.81 | 1.60 | 2.46 | 12.19 | 8.78 | 8.61 |
| **httpmorph** | 3033.50 | 2.01 | 0.71 | 1.22 | 21.74 | 18.40 | 17.46 |
| **httpx** | 775.96 | 2.98 | 3.83 | 3.86 | 12.28 | 8.54 | 8.71 |
| **pycurl** | 1910.67 | 2.15 | 2.41 | 1.77 | 8.22 | 6.44 | 8.59 |
| **requests** | 779.51 | 9.10 | N/A | 3.74 | 19.91 | N/A | 16.67 |
| **urllib** | 24.61 | 3.19 | N/A | 1.42 | 12.20 | N/A | 7.06 |
| **urllib3** | 1264.03 | 4.32 | N/A | 6.97 | 20.85 | N/A | 17.05 |

**Winners (Concurrent):**
- Local HTTP: **httpmorph** (3033.50 req/s)
- Proxy HTTP: **requests** (9.10 req/s)
- Proxy HTTP2: **httpx** (3.83 req/s)
- Proxy HTTPs: **urllib3** (6.97 req/s)
- Remote HTTP: **httpmorph** (21.74 req/s)
- Remote HTTP2: **httpmorph** (18.40 req/s)
- Remote HTTPs: **httpmorph** (17.46 req/s)

## Async Tests (Higher is Better)

Throughput in requests per second

| Library | Local HTTP | Proxy HTTP | Proxy HTTP2 | Proxy HTTPs | Remote HTTP | Remote HTTP2 | Remote HTTPs |
|---------|--------:|--------:|--------:|--------:|--------:|--------:|--------:|
| **aiohttp** | 117.14 | 4.23 | N/A | 3.14 | 59.31 | N/A | 15.47 |
| **httpmorph** | 193.53 | 3.18 | 1.50 | 1.82 | 47.96 | 15.45 | 9.68 |
| **httpx** | 131.47 | 2.45 | 2.42 | 2.66 | 51.84 | 15.43 | 9.38 |

**Winners (Async):**
- Local HTTP: **httpmorph** (193.53 req/s)
- Proxy HTTP: **aiohttp** (4.23 req/s)
- Proxy HTTP2: **httpx** (2.42 req/s)
- Proxy HTTPs: **aiohttp** (3.14 req/s)
- Remote HTTP: **aiohttp** (59.31 req/s)
- Remote HTTP2: **httpmorph** (15.45 req/s)
- Remote HTTPs: **aiohttp** (15.47 req/s)

## Overall Performance Summary

### Sequential Tests: httpmorph vs requests Speedup

| Test | httpmorph | requests | Speedup |
|------|----------:|---------:|--------:|
| Local HTTP | 0.14ms | 1.16ms | **8.06x** faster |
| Proxy HTTP | 1170.21ms | 374.35ms | 0.32x slower |
| Proxy HTTPs | 2048.53ms | 467.73ms | 0.23x slower |
| Remote HTTP | 189.68ms | 194.14ms | **1.02x** faster |
| Remote HTTPs | 188.44ms | 189.35ms | **1.00x** faster |

### Concurrent Tests: httpmorph vs requests Speedup

| Test | httpmorph | requests | Speedup |
|------|----------:|---------:|--------:|
| Local HTTP | 3033.50 req/s | 779.51 req/s | **3.89x** faster |
| Proxy HTTP | 2.01 req/s | 9.10 req/s | 0.22x slower |
| Proxy HTTPs | 1.22 req/s | 3.74 req/s | 0.33x slower |
| Remote HTTP | 21.74 req/s | 19.91 req/s | **1.09x** faster |
| Remote HTTPs | 17.46 req/s | 16.67 req/s | **1.05x** faster |

### Async Tests: httpmorph vs httpx Speedup

| Test | httpmorph | httpx | Speedup |
|------|----------:|------:|--------:|
| Local HTTP | 193.53 req/s | 131.47 req/s | **1.47x** faster |
| Proxy HTTP | 3.18 req/s | 2.45 req/s | **1.30x** faster |
| Proxy HTTP2 | 1.50 req/s | 2.42 req/s | 0.62x slower |
| Proxy HTTPs | 1.82 req/s | 2.66 req/s | 0.68x slower |
| Remote HTTP | 47.96 req/s | 51.84 req/s | 0.93x slower |
| Remote HTTP2 | 15.45 req/s | 15.43 req/s | **1.00x** faster |
| Remote HTTPs | 9.68 req/s | 9.38 req/s | **1.03x** faster |

---
*Generated by httpmorph benchmark suite*
