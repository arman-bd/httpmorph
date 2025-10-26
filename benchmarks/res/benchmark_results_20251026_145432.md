# HTTP Library Benchmark Results
**Generated:** 2025-10-26 14:54:32
**Requests per test:** 25
**Warmup requests:** 5

## Performance Summary (Mean Response Time in ms)

| Library | Local HTTP | Remote HTTP | Remote HTTPS | HTTP/2 |
|---------|-------:|-------:|-------:|-------:|
| **httpmorph** | 0.33 | 176.85 | 116.36 | 574.26 |
| **requests** | 0.87 | 347.48 | 365.60 | N/A |
| **httpx** | 0.68 | 348.53 | 344.77 | 469.02 |
| **aiohttp** | 0.48 | 355.63 | 401.31 | N/A |
| **urllib3** | 0.28 | 349.54 | 367.02 | N/A |
| **urllib** | 15.46 | 374.91 | 374.08 | N/A |

## Detailed Statistics

### Local HTTP

| Library | Mean (ms) | Median (ms) | Min (ms) | Max (ms) | Std Dev (ms) |
|---------|----------:|-----------:|---------:|---------:|-------------:|
| 🥇 **urllib3** | 0.28 | 0.27 | 0.24 | 0.37 | 0.04 |
| **httpmorph** | 0.33 | 0.31 | 0.26 | 0.45 | 0.06 |
| **aiohttp** | 0.48 | 0.46 | 0.42 | 0.82 | 0.08 |
| **httpx** | 0.68 | 0.67 | 0.60 | 0.90 | 0.07 |
| **requests** | 0.87 | 0.84 | 0.74 | 1.16 | 0.10 |
| **urllib** | 15.46 | 15.02 | 14.42 | 26.36 | 2.29 |

### Remote HTTP

| Library | Mean (ms) | Median (ms) | Min (ms) | Max (ms) | Std Dev (ms) |
|---------|----------:|-----------:|---------:|---------:|-------------:|
| 🥇 **httpmorph** | 176.85 | 175.18 | 169.15 | 192.58 | 6.31 |
| **requests** | 347.48 | 347.05 | 337.18 | 361.22 | 5.62 |
| **httpx** | 348.53 | 346.88 | 337.25 | 369.63 | 7.65 |
| **urllib3** | 349.54 | 348.77 | 335.63 | 372.99 | 9.10 |
| **aiohttp** | 355.63 | 348.95 | 333.71 | 486.29 | 28.76 |
| **urllib** | 374.91 | 374.79 | 358.62 | 404.40 | 11.00 |

### Remote HTTPS

| Library | Mean (ms) | Median (ms) | Min (ms) | Max (ms) | Std Dev (ms) |
|---------|----------:|-----------:|---------:|---------:|-------------:|
| 🥇 **httpmorph** | 116.36 | 115.41 | 110.22 | 125.94 | 3.76 |
| **httpx** | 344.77 | 344.70 | 333.58 | 361.29 | 6.40 |
| **requests** | 365.60 | 363.44 | 351.62 | 388.19 | 9.07 |
| **urllib3** | 367.02 | 366.78 | 355.58 | 381.04 | 6.32 |
| **urllib** | 374.08 | 371.79 | 360.07 | 420.49 | 12.95 |
| **aiohttp** | 401.31 | 346.68 | 333.54 | 1006.12 | 164.89 |

### HTTP/2

| Library | Mean (ms) | Median (ms) | Min (ms) | Max (ms) | Std Dev (ms) |
|---------|----------:|-----------:|---------:|---------:|-------------:|
| 🥇 **httpx** | 469.02 | 396.54 | 380.48 | 2068.40 | 334.59 |
| **httpmorph** | 574.26 | 573.50 | 553.43 | 602.75 | 11.02 |

## Speedup vs requests (baseline)

| Scenario | httpmorph | httpx | aiohttp | urllib3 | urllib |
|----------|----------:|------:|--------:|--------:|-------:|
| **Local HTTP** | 2.66x | 1.27x | 1.83x | 3.09x | 17.78x slower |
| **Remote HTTP** | 1.96x | 1.00x slower | 1.02x slower | 1.01x slower | 1.08x slower |
| **Remote HTTPS** | 3.14x | 1.06x | 1.10x slower | 1.00x slower | 1.02x slower |

## Key Findings

### Fastest Library per Scenario

- **Local HTTP**: urllib3 (0.28ms)
- **Remote HTTP**: httpmorph (176.85ms)
- **Remote HTTPS**: httpmorph (116.36ms)
- **HTTP/2**: httpx (469.02ms)

