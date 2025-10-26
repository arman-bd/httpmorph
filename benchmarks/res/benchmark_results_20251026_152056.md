# HTTP Library Benchmark Results
**Generated:** 2025-10-26 15:20:56
**Requests per test:** 100
**Warmup requests:** 10

## Performance Summary (Mean Response Time in ms)

| Library | Local HTTP | Remote HTTP | Remote HTTPS | HTTP/2 |
|---------|-------:|-------:|-------:|-------:|
| **httpmorph** | 0.27 | 190.50 | 115.29 | 582.77 |
| **requests** | 0.91 | 348.71 | 373.18 | N/A |
| **httpx** | 0.91 | 348.71 | 349.50 | 446.78 |
| **aiohttp** | 0.83 | 349.24 | 350.73 | N/A |
| **urllib3** | 0.62 | 351.44 | 372.47 | N/A |
| **urllib** | 23.19 | 380.34 | 374.64 | N/A |

## Detailed Statistics

### Local HTTP

| Library | Mean (ms) | Median (ms) | Min (ms) | Max (ms) | Std Dev (ms) |
|---------|----------:|-----------:|---------:|---------:|-------------:|
| 🥇 **httpmorph** | 0.27 | 0.26 | 0.21 | 0.43 | 0.05 |
| **urllib3** | 0.62 | 0.51 | 0.28 | 1.88 | 0.30 |
| **aiohttp** | 0.83 | 0.60 | 0.53 | 9.97 | 1.07 |
| **httpx** | 0.91 | 0.87 | 0.78 | 1.42 | 0.12 |
| **requests** | 0.91 | 0.87 | 0.78 | 1.38 | 0.11 |
| **urllib** | 23.19 | 21.97 | 21.20 | 42.03 | 4.53 |

### Remote HTTP

| Library | Mean (ms) | Median (ms) | Min (ms) | Max (ms) | Std Dev (ms) |
|---------|----------:|-----------:|---------:|---------:|-------------:|
| 🥇 **httpmorph** | 190.50 | 184.19 | 170.41 | 507.41 | 34.62 |
| **requests** | 348.71 | 346.43 | 335.25 | 406.99 | 9.68 |
| **httpx** | 348.71 | 347.18 | 336.60 | 374.52 | 8.16 |
| **aiohttp** | 349.24 | 347.87 | 333.07 | 396.33 | 9.90 |
| **urllib3** | 351.44 | 349.47 | 334.77 | 426.70 | 12.95 |
| **urllib** | 380.34 | 374.82 | 353.66 | 488.01 | 21.67 |

### Remote HTTPS

| Library | Mean (ms) | Median (ms) | Min (ms) | Max (ms) | Std Dev (ms) |
|---------|----------:|-----------:|---------:|---------:|-------------:|
| 🥇 **httpmorph** | 115.29 | 115.45 | 108.52 | 138.60 | 4.60 |
| **httpx** | 349.50 | 348.12 | 336.23 | 403.99 | 8.97 |
| **aiohttp** | 350.73 | 348.56 | 337.80 | 438.32 | 10.84 |
| **urllib3** | 372.47 | 371.04 | 352.37 | 399.97 | 8.46 |
| **requests** | 373.18 | 369.50 | 352.91 | 482.33 | 16.24 |
| **urllib** | 374.64 | 372.64 | 356.95 | 409.68 | 10.43 |

### HTTP/2

| Library | Mean (ms) | Median (ms) | Min (ms) | Max (ms) | Std Dev (ms) |
|---------|----------:|-----------:|---------:|---------:|-------------:|
| 🥇 **httpx** | 446.78 | 408.44 | 393.70 | 1416.20 | 182.28 |
| **httpmorph** | 582.77 | 580.91 | 550.84 | 715.13 | 21.10 |

## Speedup vs requests (baseline)

| Scenario | httpmorph | httpx | aiohttp | urllib3 | urllib |
|----------|----------:|------:|--------:|--------:|-------:|
| **Local HTTP** | 3.34x | 1.01x | 1.10x | 1.48x | 25.46x slower |
| **Remote HTTP** | 1.83x | 1.00x slower | 1.00x slower | 1.01x slower | 1.09x slower |
| **Remote HTTPS** | 3.24x | 1.07x | 1.06x | 1.00x | 1.00x slower |

## Key Findings

### Fastest Library per Scenario

- **Local HTTP**: httpmorph (0.27ms)
- **Remote HTTP**: httpmorph (190.50ms)
- **Remote HTTPS**: httpmorph (115.29ms)
- **HTTP/2**: httpx (446.78ms)

