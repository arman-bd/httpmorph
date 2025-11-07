# httpmorph Benchmark Results

**Version:** 0.2.4 | **Generated:** 2025-11-07

## System Information

| Property | Value |
|----------|-------|
| **OS** | Windows (Windows-10-10.0.20348-SP0) |
| **Processor** | AMD64 Family 23 Model 1 Stepping 2, AuthenticAMD |
| **CPU Cores** | 3 |
| **Python** | 3.11.9 (CPython) |

## Test Configuration

- **Sequential Requests:** 25 (warmup: 5)
- **Concurrent Requests:** 25 (workers: 5)

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
| **curl_cffi** | 0.89ms | 1382.07ms | 1637.57ms | 1252.71ms | 1.66ms | 3.40ms | 3.43ms |
| **httpmorph** | 6.88ms | 1334.71ms | 1676.44ms | 1571.33ms | 1.69ms | 13.41ms | 5.78ms |
| **httpx** | 3.35ms | 342.21ms | 441.69ms | 402.97ms | 8.17ms | 17.26ms | 8.97ms |
| **pycurl** | 0.59ms | 821.61ms | 1255.96ms | 1529.15ms | 1.19ms | 8.58ms | 6.62ms |
| **requests** | 6.30ms | 362.45ms | N/A | 333.30ms | 2.12ms | N/A | 1.64ms |
| **urllib** | 16.74ms | 1316.49ms | N/A | 1316.87ms | 3.82ms | N/A | 23.50ms |
| **urllib3** | 1.41ms | 321.69ms | N/A | 370.93ms | 0.85ms | N/A | 0.71ms |

**Winners (Sequential):**
- Local HTTP: **pycurl** (0.59ms)
- Proxy HTTP: **urllib3** (321.69ms)
- Proxy HTTP2: **httpx** (441.69ms)
- Proxy HTTPs: **requests** (333.30ms)
- Remote HTTP: **urllib3** (0.85ms)
- Remote HTTP2: **curl_cffi** (3.40ms)
- Remote HTTPs: **urllib3** (0.71ms)

## Concurrent Tests (Higher is Better)

Throughput in requests per second

| Library | Local HTTP | Proxy HTTP | Proxy HTTP2 | Proxy HTTPs | Remote HTTP | Remote HTTP2 | Remote HTTPs |
|---------|--------:|--------:|--------:|--------:|--------:|--------:|--------:|
| **curl_cffi** | 654.76 | 2.91 | 2.06 | 2.98 | 410.53 | 571.77 | 551.06 |
| **httpmorph** | 936.27 | 6.04 | 3.28 | 2.15 | 823.39 | 478.66 | 573.54 |
| **httpx** | 436.32 | 4.42 | 2.73 | 9.00 | 441.09 | 272.52 | 374.32 |
| **pycurl** | 1698.89 | 3.51 | 2.26 | 1.80 | 1084.42 | 384.91 | 424.51 |
| **requests** | 460.28 | 4.30 | N/A | 7.55 | 615.74 | N/A | 201.24 |
| **urllib** | 56.43 | 3.94 | N/A | 1.88 | 790.93 | N/A | 60.85 |
| **urllib3** | 1011.78 | 11.59 | N/A | 7.23 | 674.37 | N/A | 217.71 |

**Winners (Concurrent):**
- Local HTTP: **pycurl** (1698.89 req/s)
- Proxy HTTP: **urllib3** (11.59 req/s)
- Proxy HTTP2: **httpmorph** (3.28 req/s)
- Proxy HTTPs: **httpx** (9.00 req/s)
- Remote HTTP: **pycurl** (1084.42 req/s)
- Remote HTTP2: **curl_cffi** (571.77 req/s)
- Remote HTTPs: **httpmorph** (573.54 req/s)

## Async Tests (Higher is Better)

Throughput in requests per second

| Library | Local HTTP | Proxy HTTP | Proxy HTTP2 | Proxy HTTPs | Remote HTTP | Remote HTTP2 | Remote HTTPs |
|---------|--------:|--------:|--------:|--------:|--------:|--------:|--------:|
| **aiohttp** | 24.26 | 4.31 | N/A | 12.24 | 788.43 | N/A | 282.15 |
| **httpmorph** | 22.74 | 66.99 | 3.66 | 3.99 | 282.35 | 205.69 | 182.08 |
| **httpx** | 23.53 | 4.27 | 3.87 | 3.82 | 354.75 | 142.11 | 228.86 |

**Winners (Async):**
- Local HTTP: **aiohttp** (24.26 req/s)
- Proxy HTTP: **httpmorph** (66.99 req/s)
- Proxy HTTP2: **httpx** (3.87 req/s)
- Proxy HTTPs: **aiohttp** (12.24 req/s)
- Remote HTTP: **aiohttp** (788.43 req/s)
- Remote HTTP2: **httpmorph** (205.69 req/s)
- Remote HTTPs: **aiohttp** (282.15 req/s)

## Overall Performance Summary

### Sequential Tests: httpmorph vs requests Speedup

| Test | httpmorph | requests | Speedup |
|------|----------:|---------:|--------:|
| Local HTTP | 6.88ms | 6.30ms | 0.92x slower |
| Proxy HTTP | 1334.71ms | 362.45ms | 0.27x slower |
| Proxy HTTPs | 1571.33ms | 333.30ms | 0.21x slower |
| Remote HTTP | 1.69ms | 2.12ms | **1.26x** faster |
| Remote HTTPs | 5.78ms | 1.64ms | 0.28x slower |

### Concurrent Tests: httpmorph vs requests Speedup

| Test | httpmorph | requests | Speedup |
|------|----------:|---------:|--------:|
| Local HTTP | 936.27 req/s | 460.28 req/s | **2.03x** faster |
| Proxy HTTP | 6.04 req/s | 4.30 req/s | **1.41x** faster |
| Proxy HTTPs | 2.15 req/s | 7.55 req/s | 0.29x slower |
| Remote HTTP | 823.39 req/s | 615.74 req/s | **1.34x** faster |
| Remote HTTPs | 573.54 req/s | 201.24 req/s | **2.85x** faster |

### Async Tests: httpmorph vs httpx Speedup

| Test | httpmorph | httpx | Speedup |
|------|----------:|------:|--------:|
| Local HTTP | 22.74 req/s | 23.53 req/s | 0.97x slower |
| Proxy HTTP | 66.99 req/s | 4.27 req/s | **15.68x** faster |
| Proxy HTTP2 | 3.66 req/s | 3.87 req/s | 0.94x slower |
| Proxy HTTPs | 3.99 req/s | 3.82 req/s | **1.05x** faster |
| Remote HTTP | 282.35 req/s | 354.75 req/s | 0.80x slower |
| Remote HTTP2 | 205.69 req/s | 142.11 req/s | **1.45x** faster |
| Remote HTTPs | 182.08 req/s | 228.86 req/s | 0.80x slower |

---
*Generated by httpmorph benchmark suite*
