"""Base class for library benchmarks"""

import asyncio
import statistics
import time
from abc import ABC, abstractmethod
from concurrent.futures import ThreadPoolExecutor


class LibraryBenchmark(ABC):
    """Base class for library-specific benchmark implementations"""

    def __init__(self, config):
        """
        Args:
            config: Benchmark configuration dictionary with:
                - num_sequential: Number of sequential requests
                - num_concurrent: Number of concurrent requests
                - concurrent_workers: Number of concurrent workers
                - warmup_requests: Number of warmup requests
                - local_url: Local HTTP server URL
                - remote_http_url: Remote HTTP URL
                - remote_https_url: Remote HTTPS URL
                - http2_url: HTTP/2 test URL
                - proxy_url_http: Proxy URL for HTTP targets
                - proxy_url_https: Proxy URL for HTTPS targets
                - proxy_target_http: HTTP target via proxy
                - proxy_target_https: HTTPS target via proxy
        """
        self.config = config
        self.num_sequential = config["num_sequential"]
        self.num_concurrent = config["num_concurrent"]
        self.concurrent_workers = config["concurrent_workers"]
        self.warmup_requests = config["warmup_requests"]
        self.local_url = config["local_url"]
        self.remote_http_url = config["remote_http_url"]
        self.remote_https_url = config["remote_https_url"]
        self.http2_url = config["http2_url"]
        self.proxy_url_http = config.get("proxy_url_http")
        self.proxy_url_https = config.get("proxy_url_https")
        self.proxy_target_http = config["proxy_target_http"]
        self.proxy_target_https = config["proxy_target_https"]

    @abstractmethod
    def get_test_matrix(self):
        """Return list of (test_name, test_key) tuples for this library"""
        pass

    @abstractmethod
    def is_available(self):
        """Return True if library is available/installed"""
        pass

    def run_sequential_benchmark(self, name, func):
        """Run sequential benchmark"""
        try:
            # Warmup
            for _ in range(self.warmup_requests):
                func()

            # Actual test
            times = []
            for _ in range(self.num_sequential):
                start = time.perf_counter()
                func()
                elapsed = (time.perf_counter() - start) * 1000  # Convert to ms
                times.append(elapsed)

            # Detailed trend analysis
            n = len(times)

            # Quartile analysis
            q1_size = n // 4
            q1 = times[:q1_size] if q1_size > 0 else times[:1]
            q2 = times[q1_size : 2 * q1_size] if q1_size > 0 else times
            q3 = times[2 * q1_size : 3 * q1_size] if q1_size > 0 else times
            q4 = times[3 * q1_size :] if q1_size > 0 else times

            # Linear regression trend (slope in ms per request)
            try:
                x_vals = list(range(n))
                x_mean = sum(x_vals) / n
                y_mean = statistics.mean(times)

                numerator = sum((x_vals[i] - x_mean) * (times[i] - y_mean) for i in range(n))
                denominator = sum((x_vals[i] - x_mean) ** 2 for i in range(n))

                trend_slope_ms = numerator / denominator if denominator != 0 else 0
            except:
                trend_slope_ms = 0

            # Stability metric (coefficient of variation)
            mean_val = statistics.mean(times)
            cv = (
                (statistics.stdev(times) / mean_val * 100) if mean_val > 0 and len(times) > 1 else 0
            )

            return {
                "mean_ms": mean_val,
                "median_ms": statistics.median(times),
                "min_ms": min(times),
                "max_ms": max(times),
                "stdev_ms": statistics.stdev(times) if len(times) > 1 else 0,
                "p50_ms": statistics.median(times),
                "p95_ms": times[int(n * 0.95)] if n > 20 else max(times),
                "p99_ms": times[int(n * 0.99)] if n > 100 else max(times),
                "timings": times,  # All individual timings
                "q1_avg_ms": statistics.mean(q1) if q1 else 0,
                "q2_avg_ms": statistics.mean(q2) if q2 else 0,
                "q3_avg_ms": statistics.mean(q3) if q3 else 0,
                "q4_avg_ms": statistics.mean(q4) if q4 else 0,
                "trend_slope_ms_per_req": trend_slope_ms,  # Linear trend
                "cv_pct": cv,  # Coefficient of variation (stability metric)
                "first_avg_ms": statistics.mean(q1) if q1 else 0,
                "last_avg_ms": statistics.mean(q4) if q4 else 0,
            }
        except Exception as e:
            return {"error": f"{type(e).__name__}: {str(e)[:100]}"}

    def run_concurrent_sync(self, func):
        """Run concurrent synchronous benchmark"""
        try:
            # Track individual completion times
            completion_times = []
            start_time = time.perf_counter()

            def timed_func():
                func()
                completion_times.append((time.perf_counter() - start_time) * 1000)

            start = time.perf_counter()
            with ThreadPoolExecutor(max_workers=self.concurrent_workers) as executor:
                futures = [executor.submit(timed_func) for _ in range(self.num_concurrent)]
                for future in futures:
                    future.result()
            total_time = time.perf_counter() - start

            # Sort by completion time to analyze progression
            completion_times.sort()

            # Detailed analysis of completion times
            n = len(completion_times)

            # Quartile analysis
            q1_size = n // 4
            q1 = completion_times[:q1_size] if q1_size > 0 else completion_times[:1]
            q4 = completion_times[3 * q1_size :] if q1_size > 0 else completion_times

            # Linear trend
            try:
                x_vals = list(range(n))
                x_mean = sum(x_vals) / n
                y_mean = statistics.mean(completion_times)

                numerator = sum(
                    (x_vals[i] - x_mean) * (completion_times[i] - y_mean) for i in range(n)
                )
                denominator = sum((x_vals[i] - x_mean) ** 2 for i in range(n))

                trend_slope_ms = numerator / denominator if denominator != 0 else 0
            except:
                trend_slope_ms = 0

            mean_val = statistics.mean(completion_times)
            cv = (
                (statistics.stdev(completion_times) / mean_val * 100)
                if mean_val > 0 and len(completion_times) > 1
                else 0
            )

            return {
                "total_time_s": total_time,
                "req_per_sec": self.num_concurrent / total_time,
                "avg_ms": (total_time / self.num_concurrent) * 1000,
                "mean_completion_ms": mean_val,
                "median_completion_ms": statistics.median(completion_times),
                "min_completion_ms": min(completion_times) if completion_times else 0,
                "max_completion_ms": max(completion_times) if completion_times else 0,
                "p95_completion_ms": completion_times[int(n * 0.95)]
                if n > 20
                else max(completion_times)
                if completion_times
                else 0,
                "completion_times": completion_times,  # All individual completion times
                "trend_slope_ms_per_req": trend_slope_ms,
                "cv_pct": cv,
                "first_avg_ms": statistics.mean(q1) if q1 else 0,
                "last_avg_ms": statistics.mean(q4) if q4 else 0,
            }
        except Exception as e:
            return {"error": f"{type(e).__name__}: {str(e)[:100]}"}

    def run_async_benchmark(self, async_func):
        """Run async benchmark"""
        try:
            completion_times = []

            async def run_test():
                start_time = time.perf_counter()

                async def timed_func():
                    await async_func()
                    completion_times.append((time.perf_counter() - start_time) * 1000)

                start = time.perf_counter()
                tasks = [timed_func() for _ in range(self.num_concurrent)]
                await asyncio.gather(*tasks)
                return time.perf_counter() - start

            total_time = asyncio.run(run_test())

            # Sort by completion time
            completion_times.sort()

            # Detailed analysis of completion times
            n = len(completion_times)

            # Quartile analysis
            q1_size = n // 4
            q1 = completion_times[:q1_size] if q1_size > 0 else completion_times[:1]
            q4 = completion_times[3 * q1_size :] if q1_size > 0 else completion_times

            # Linear trend
            try:
                x_vals = list(range(n))
                x_mean = sum(x_vals) / n
                y_mean = statistics.mean(completion_times)

                numerator = sum(
                    (x_vals[i] - x_mean) * (completion_times[i] - y_mean) for i in range(n)
                )
                denominator = sum((x_vals[i] - x_mean) ** 2 for i in range(n))

                trend_slope_ms = numerator / denominator if denominator != 0 else 0
            except:
                trend_slope_ms = 0

            mean_val = statistics.mean(completion_times)
            cv = (
                (statistics.stdev(completion_times) / mean_val * 100)
                if mean_val > 0 and len(completion_times) > 1
                else 0
            )

            return {
                "total_time_s": total_time,
                "req_per_sec": self.num_concurrent / total_time,
                "avg_ms": (total_time / self.num_concurrent) * 1000,
                "mean_completion_ms": mean_val,
                "median_completion_ms": statistics.median(completion_times),
                "min_completion_ms": min(completion_times) if completion_times else 0,
                "max_completion_ms": max(completion_times) if completion_times else 0,
                "p95_completion_ms": completion_times[int(n * 0.95)]
                if n > 20
                else max(completion_times)
                if completion_times
                else 0,
                "completion_times": completion_times,  # All individual completion times
                "trend_slope_ms_per_req": trend_slope_ms,
                "cv_pct": cv,
                "first_avg_ms": statistics.mean(q1) if q1 else 0,
                "last_avg_ms": statistics.mean(q4) if q4 else 0,
            }
        except Exception as e:
            return {"error": f"{type(e).__name__}: {str(e)[:100]}"}
