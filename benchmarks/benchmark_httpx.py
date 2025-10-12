#!/usr/bin/env python3
"""
Performance benchmark comparing httpmorph against the httpx library.

This benchmark measures throughput (requests/second) and latency for both
GET and POST requests against a local httpbin server.
"""

import platform
import subprocess
import sys
import time
from pathlib import Path
from statistics import mean, median, stdev
from typing import List, Tuple

try:
    import httpmorph
    HTTPMORPH_AVAILABLE = True
except ImportError:
    HTTPMORPH_AVAILABLE = False
    print("⚠️  httpmorph not installed. Install with: pip install -e .")

try:
    import httpx
    HTTPX_AVAILABLE = True
except ImportError:
    HTTPX_AVAILABLE = False
    print("⚠️  httpx not installed. Install with: pip install httpx")


class BenchmarkResult:
    """Container for benchmark results."""

    def __init__(self, name: str):
        self.name = name
        self.latencies: List[float] = []
        self.errors: int = 0
        self.total_time: float = 0.0

    @property
    def total_requests(self) -> int:
        return len(self.latencies) + self.errors

    @property
    def success_rate(self) -> float:
        if self.total_requests == 0:
            return 0.0
        return (len(self.latencies) / self.total_requests) * 100

    @property
    def requests_per_second(self) -> float:
        if self.total_time == 0:
            return 0.0
        return self.total_requests / self.total_time

    @property
    def avg_latency_ms(self) -> float:
        if not self.latencies:
            return 0.0
        return mean(self.latencies) * 1000

    @property
    def median_latency_ms(self) -> float:
        if not self.latencies:
            return 0.0
        return median(self.latencies) * 1000

    @property
    def p95_latency_ms(self) -> float:
        if not self.latencies:
            return 0.0
        sorted_latencies = sorted(self.latencies)
        index = int(len(sorted_latencies) * 0.95)
        return sorted_latencies[index] * 1000

    @property
    def p99_latency_ms(self) -> float:
        if not self.latencies:
            return 0.0
        sorted_latencies = sorted(self.latencies)
        index = int(len(sorted_latencies) * 0.99)
        return sorted_latencies[index] * 1000

    @property
    def stdev_latency_ms(self) -> float:
        if len(self.latencies) < 2:
            return 0.0
        return stdev(self.latencies) * 1000


def check_docker() -> bool:
    """Check if Docker is available."""
    try:
        subprocess.run(
            ["docker", "--version"],
            check=True,
            capture_output=True,
            text=True
        )
        return True
    except (subprocess.CalledProcessError, FileNotFoundError):
        return False


def check_or_pull_image(image_name: str) -> bool:
    """Check if Docker image exists, pull if not."""
    print(f"🔍 Checking for Docker image: {image_name}...")

    # Check if image exists locally
    try:
        result = subprocess.run(
            ["docker", "images", "-q", image_name],
            check=True,
            capture_output=True,
            text=True
        )
        if result.stdout.strip():
            print(f"✅ Image already exists locally")
            return True
    except subprocess.CalledProcessError:
        pass

    # Image doesn't exist, pull it
    print(f"📥 Pulling Docker image {image_name}...")
    try:
        subprocess.run(
            ["docker", "pull", image_name],
            check=True,
            text=True
        )
        print(f"✅ Docker image pulled successfully")
        return True

    except subprocess.CalledProcessError as e:
        print(f"❌ Failed to pull Docker image: {e}")
        return False


def start_httpbingo(port: int = 8080, image_name: str = "mccutchen/go-httpbin") -> Tuple[bool, str]:
    """Start httpbingo Docker container."""
    print(f"🐳 Starting httpbingo container on port {port}...")

    # Stop any existing container
    subprocess.run(
        ["docker", "rm", "-f", "httpmorph-benchmark"],
        capture_output=True
    )

    try:
        # Run container
        result = subprocess.run(
            [
                "docker", "run", "-d",
                "--name", "httpmorph-benchmark",
                "-p", f"{port}:8080",
                image_name
            ],
            check=True,
            capture_output=True,
            text=True
        )
        container_id = result.stdout.strip()

        # Wait for container to be ready
        print("⏳ Waiting for container to be ready...")
        for _ in range(30):
            try:
                if HTTPMORPH_AVAILABLE:
                    resp = httpmorph.get(f"http://localhost:{port}/status/200", timeout=1)
                    if resp.status_code == 200:
                        break
                elif HTTPX_AVAILABLE:
                    resp = httpx.get(f"http://localhost:{port}/status/200", timeout=1)
                    if resp.status_code == 200:
                        break
            except Exception:
                time.sleep(0.5)
        else:
            print("❌ Container failed to start in time")
            return False, ""

        print(f"✅ httpbingo container started (ID: {container_id[:12]})")
        return True, container_id

    except subprocess.CalledProcessError as e:
        print(f"❌ Failed to start container: {e}")
        if e.stderr:
            print(f"   Error: {e.stderr}")
        return False, ""


def stop_httpbingo():
    """Stop and remove httpbingo container."""
    print("\n🛑 Stopping container...")
    subprocess.run(
        ["docker", "rm", "-f", "httpmorph-benchmark"],
        capture_output=True
    )
    print("✅ Container stopped")


def benchmark_httpmorph_get(url: str, duration_seconds: int) -> BenchmarkResult:
    """Benchmark httpmorph GET requests."""
    result = BenchmarkResult("httpmorph GET")

    print(f"\n{'='*60}")
    print(f"📊 Benchmarking httpmorph GET requests for {duration_seconds}s")
    print(f"{'='*60}")

    start_time = time.time()
    end_time = start_time + duration_seconds

    while time.time() < end_time:
        request_start = time.time()
        try:
            response = httpmorph.get(url, timeout=5)
            request_end = time.time()

            if response.status_code == 200:
                result.latencies.append(request_end - request_start)
            else:
                result.errors += 1
        except Exception:
            result.errors += 1

    result.total_time = time.time() - start_time
    return result


def benchmark_httpmorph_post(url: str, duration_seconds: int) -> BenchmarkResult:
    """Benchmark httpmorph POST requests."""
    result = BenchmarkResult("httpmorph POST")

    print(f"\n{'='*60}")
    print(f"📊 Benchmarking httpmorph POST requests for {duration_seconds}s")
    print(f"{'='*60}")

    payload = {"test": "data", "timestamp": time.time()}

    start_time = time.time()
    end_time = start_time + duration_seconds

    while time.time() < end_time:
        request_start = time.time()
        try:
            response = httpmorph.post(url, json=payload, timeout=5)
            request_end = time.time()

            if response.status_code == 200:
                result.latencies.append(request_end - request_start)
            else:
                result.errors += 1
        except Exception:
            result.errors += 1

    result.total_time = time.time() - start_time
    return result


def benchmark_httpx_get(url: str, duration_seconds: int) -> BenchmarkResult:
    """Benchmark httpx GET requests."""
    result = BenchmarkResult("httpx GET")

    print(f"\n{'='*60}")
    print(f"📊 Benchmarking httpx GET requests for {duration_seconds}s")
    print(f"{'='*60}")

    start_time = time.time()
    end_time = start_time + duration_seconds

    client = httpx.Client()

    while time.time() < end_time:
        request_start = time.time()
        try:
            response = client.get(url, timeout=5)
            request_end = time.time()

            if response.status_code == 200:
                result.latencies.append(request_end - request_start)
            else:
                result.errors += 1
        except Exception:
            result.errors += 1

    result.total_time = time.time() - start_time
    client.close()
    return result


def benchmark_httpx_post(url: str, duration_seconds: int) -> BenchmarkResult:
    """Benchmark httpx POST requests."""
    result = BenchmarkResult("httpx POST")

    print(f"\n{'='*60}")
    print(f"📊 Benchmarking httpx POST requests for {duration_seconds}s")
    print(f"{'='*60}")

    payload = {"test": "data", "timestamp": time.time()}

    start_time = time.time()
    end_time = start_time + duration_seconds

    client = httpx.Client()

    while time.time() < end_time:
        request_start = time.time()
        try:
            response = client.post(url, json=payload, timeout=5)
            request_end = time.time()

            if response.status_code == 200:
                result.latencies.append(request_end - request_start)
            else:
                result.errors += 1
        except Exception:
            result.errors += 1

    result.total_time = time.time() - start_time
    client.close()
    return result


def print_results(results: List[BenchmarkResult]):
    """Print formatted benchmark results."""
    print(f"\n{'=' * 80}")
    print(f"{'BENCHMARK RESULTS':^80}")
    print(f"{'=' * 80}\n")

    # Summary table
    print(f"{'Metric':<25} {'httpmorph GET':<15} {'httpmorph POST':<15} {'httpx GET':<15} {'httpx POST':<15}")
    print(f"{'-'*25} {'-'*15} {'-'*15} {'-'*15} {'-'*15}")

    metrics = [
        ("Total Requests", lambda r: f"{r.total_requests:,}"),
        ("Successful", lambda r: f"{len(r.latencies):,}"),
        ("Errors", lambda r: f"{r.errors:,}"),
        ("Success Rate", lambda r: f"{r.success_rate:.2f}%"),
        ("Req/sec", lambda r: f"{r.requests_per_second:.2f}"),
        ("", lambda r: ""),  # Empty row
        ("Avg Latency (ms)", lambda r: f"{r.avg_latency_ms:.2f}"),
        ("Median Latency (ms)", lambda r: f"{r.median_latency_ms:.2f}"),
        ("P95 Latency (ms)", lambda r: f"{r.p95_latency_ms:.2f}"),
        ("P99 Latency (ms)", lambda r: f"{r.p99_latency_ms:.2f}"),
        ("StdDev Latency (ms)", lambda r: f"{r.stdev_latency_ms:.2f}"),
    ]

    for metric_name, metric_func in metrics:
        if metric_name == "":
            print()
            continue
        row = f"{metric_name:<25}"
        for result in results:
            row += f" {metric_func(result):<15}"
        print(row)

    print("\n" + "=" * 80)

    # Performance comparison
    print(f"\n{'PERFORMANCE COMPARISON':^80}")
    print("=" * 80 + "\n")

    httpmorph_get, httpmorph_post, httpx_get, httpx_post = results

    # GET comparison
    if httpx_get.requests_per_second > 0:
        speedup = httpmorph_get.requests_per_second / httpx_get.requests_per_second
        print("📈 GET Requests:")
        print(f"   httpmorph: {httpmorph_get.requests_per_second:.2f} req/s")
        print(f"   httpx:     {httpx_get.requests_per_second:.2f} req/s")
        if speedup > 1:
            print(f"   Result: {speedup:.2f}x faster")
        else:
            print(f"   Result: {1/speedup:.2f}x slower")

    # POST comparison
    if httpx_post.requests_per_second > 0:
        speedup = httpmorph_post.requests_per_second / httpx_post.requests_per_second
        print("\n📈 POST Requests:")
        print(f"   httpmorph: {httpmorph_post.requests_per_second:.2f} req/s")
        print(f"   httpx:     {httpx_post.requests_per_second:.2f} req/s")
        if speedup > 1:
            print(f"   Result: {speedup:.2f}x faster")
        else:
            print(f"   Result: {1/speedup:.2f}x slower")

    # Latency comparison
    print("\n📉 Latency:")
    print(f"   GET - httpmorph: {httpmorph_get.avg_latency_ms:.2f}ms, "
          f"httpx: {httpx_get.avg_latency_ms:.2f}ms")
    print(f"   POST - httpmorph: {httpmorph_post.avg_latency_ms:.2f}ms, "
          f"httpx: {httpx_post.avg_latency_ms:.2f}ms")

    print("\n" + "=" * 80 + "\n")


def main():
    """Main benchmark execution."""
    print("\n" + "=" * 80)
    print(f"{'httpmorph vs httpx - Performance Benchmark':^80}")
    print("=" * 80 + "\n")

    # Check prerequisites
    if not HTTPMORPH_AVAILABLE:
        print("❌ httpmorph is not available. Cannot run benchmark.")
        sys.exit(1)

    if not HTTPX_AVAILABLE:
        print("❌ httpx is not available. Cannot run benchmark.")
        sys.exit(1)

    if not check_docker():
        print("❌ Docker is not available. Please install Docker to run this benchmark.")
        sys.exit(1)

    # Configuration
    PORT = 8080
    DURATION = 10  # seconds per benchmark
    BASE_URL = f"http://localhost:{PORT}"
    IMAGE_NAME = "mccutchen/go-httpbin:latest"

    print("⚙️  Configuration:")
    print(f"   Test Duration: {DURATION} seconds per test")
    print(f"   Base URL: {BASE_URL}")
    print(f"   Docker Image: {IMAGE_NAME}")
    print("   Total Tests: 4 (GET/POST × httpmorph/httpx)\n")

    # Check or pull Docker image
    if not check_or_pull_image(IMAGE_NAME):
        print("\n❌ Failed to get Docker image.")
        sys.exit(1)

    # Start httpbingo container
    success, _container_id = start_httpbingo(PORT, IMAGE_NAME)
    if not success:
        sys.exit(1)

    try:
        time.sleep(2)  # Give server a moment to stabilize

        # Run benchmarks
        results = []

        # httpmorph GET
        results.append(benchmark_httpmorph_get(f"{BASE_URL}/get", DURATION))
        time.sleep(1)

        # httpmorph POST
        results.append(benchmark_httpmorph_post(f"{BASE_URL}/post", DURATION))
        time.sleep(1)

        # httpx GET
        results.append(benchmark_httpx_get(f"{BASE_URL}/get", DURATION))
        time.sleep(1)

        # httpx POST
        results.append(benchmark_httpx_post(f"{BASE_URL}/post", DURATION))

        # Print results
        print_results(results)

    finally:
        # Cleanup
        stop_httpbingo()

    print("✅ Benchmark completed successfully!\n")


if __name__ == "__main__":
    main()
