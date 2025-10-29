#!/usr/bin/env python3
"""
Comprehensive HTTP Library Benchmark

Tests ALL libraries across ALL dimensions:
- Sync vs Async
- Sequential vs Concurrent
- Local vs Remote
- HTTP vs HTTPS
- HTTP/1.1 vs HTTP/2
- Proxy HTTP vs Proxy HTTPS

Libraries tested:
- httpmorph (sync + async + HTTP/2) - 19 tests
- httpx (sync + async + HTTP/2) - 19 tests
- requests (sync only) - 9 tests
- aiohttp (async only) - 4 tests
- urllib3 (sync only) - 9 tests
- urllib (sync only) - 9 tests
- pycurl (sync + HTTP/2) - 12 tests
- curl_cffi (sync + HTTP/2) - 12 tests
"""

import json
import os
import platform
import sys
import threading
import time
from datetime import datetime
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

# Optional imports for graphics
try:
    import matplotlib
    matplotlib.use('Agg')  # Non-interactive backend
    import matplotlib.pyplot as plt
    import numpy as np
    MATPLOTLIB_AVAILABLE = True
except ImportError:
    MATPLOTLIB_AVAILABLE = False

# Add script directory to sys.path for libs imports
sys.path.insert(0, str(Path(__file__).parent))

# Load environment variables
try:
    from dotenv import load_dotenv
    env_path = Path(__file__).parent.parent / '.env'
    if env_path.exists():
        load_dotenv(env_path)
except ImportError:
    pass

# Import library benchmark classes (after sys.path modification)
from libs.aiohttp_bench import AiohttpBenchmark  # noqa: E402
from libs.curl_cffi_bench import CurlCffiBenchmark  # noqa: E402
from libs.httpmorph_bench import HttpmorphBenchmark  # noqa: E402
from libs.httpx_bench import HttpxBenchmark  # noqa: E402
from libs.pycurl_bench import PycurlBenchmark  # noqa: E402
from libs.requests_bench import RequestsBenchmark  # noqa: E402
from libs.urllib3_bench import Urllib3Benchmark  # noqa: E402
from libs.urllib_bench import UrllibBenchmark  # noqa: E402


def get_system_info():
    """Collect system information for benchmark metadata"""
    info = {
        'os': platform.system(),
        'os_version': platform.version(),
        'platform': platform.platform(),
        'processor': platform.processor() or platform.machine(),
        'python_version': sys.version.split()[0],
        'python_implementation': platform.python_implementation(),
    }

    # Try to get CPU count
    try:
        import multiprocessing
        info['cpu_count'] = multiprocessing.cpu_count()
    except Exception:
        info['cpu_count'] = 'unknown'

    # Try to get memory info (Unix-like systems)
    try:
        import subprocess
        if platform.system() == 'Darwin':  # macOS
            result = subprocess.run(['sysctl', '-n', 'hw.memsize'],
                                  capture_output=True, text=True, timeout=1)
            if result.returncode == 0:
                mem_bytes = int(result.stdout.strip())
                info['memory_gb'] = round(mem_bytes / (1024**3), 2)
        elif platform.system() == 'Linux':
            with open('/proc/meminfo') as f:
                for line in f:
                    if line.startswith('MemTotal:'):
                        mem_kb = int(line.split()[1])
                        info['memory_gb'] = round(mem_kb / (1024**2), 2)
                        break
    except Exception:
        info['memory_gb'] = 'unknown'

    return info


# Simple HTTP server
class SimpleHTTPHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header('Content-Type', 'text/plain')
        self.end_headers()
        self.wfile.write(b'OK')

    def log_message(self, _format, *_args):
        pass


class BenchmarkServer:
    def __init__(self, port=18891):
        self.port = port
        self.server = HTTPServer(('127.0.0.1', port), SimpleHTTPHandler)
        self.thread = None

    def start(self):
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        time.sleep(0.5)

    def stop(self):
        self.server.shutdown()
        if self.thread:
            self.thread.join(timeout=2)


class Benchmark:
    def __init__(self, num_sequential=50, warmup=5, num_concurrent=25, concurrent_workers=10):
        self.num_sequential = num_sequential
        self.warmup = warmup
        self.num_concurrent = num_concurrent
        self.concurrent_workers = concurrent_workers

        self.local_port = 18891
        self.local_url = f'http://127.0.0.1:{self.local_port}/'
        self.remote_http_url = 'http://www.google.com/'
        self.remote_https_url = 'https://www.google.com/'
        self.http2_url = 'https://www.google.com/'

        # Proxy configuration - separate proxies for HTTP and HTTPS targets
        self.proxy_url_http = os.environ.get('TEST_PROXY_URL')
        self.proxy_url_https = os.environ.get('TEST_PROXY_URL')
        # Fallback to TEST_PROXY_URL if separate proxies not configured
        if not self.proxy_url_http:
            self.proxy_url_http = os.environ.get('TEST_PROXY_URL')
        if not self.proxy_url_https:
            self.proxy_url_https = os.environ.get('TEST_PROXY_URL')
        self.proxy_target_http = 'http://www.google.com/'         # HTTP target via proxy
        self.proxy_target_https = 'https://www.google.com/'    # HTTPS target via proxy

        self.results = {}

        # Create config dict for library benchmarks
        config = {
            'num_sequential': num_sequential,
            'num_concurrent': num_concurrent,
            'concurrent_workers': concurrent_workers,
            'warmup_requests': warmup,
            'local_url': self.local_url,
            'remote_http_url': self.remote_http_url,
            'remote_https_url': self.remote_https_url,
            'http2_url': self.http2_url,
            'proxy_url_http': self.proxy_url_http,
            'proxy_url_https': self.proxy_url_https,
            'proxy_target_http': self.proxy_target_http,
            'proxy_target_https': self.proxy_target_https,
        }

        # Initialize library benchmarks
        self.lib_benchmarks = {
            'httpmorph': HttpmorphBenchmark(config),
            'requests': RequestsBenchmark(config),
            'httpx': HttpxBenchmark(config),
            'aiohttp': AiohttpBenchmark(config),
            'urllib3': Urllib3Benchmark(config),
            'urllib': UrllibBenchmark(config),
            'pycurl': PycurlBenchmark(config),
            'curl_cffi': CurlCffiBenchmark(config),
        }

        self.library_versions = {}

    def get_library_versions(self):
        """Detect and return versions of all available libraries"""
        versions = {}

        # httpmorph
        try:
            import httpmorph
            versions['httpmorph'] = httpmorph.__version__ if hasattr(httpmorph, '__version__') else 'unknown'
        except ImportError:
            versions['httpmorph'] = 'not installed'

        # requests
        try:
            import requests
            versions['requests'] = requests.__version__
        except ImportError:
            versions['requests'] = 'not installed'

        # httpx
        try:
            import httpx
            versions['httpx'] = httpx.__version__
        except ImportError:
            versions['httpx'] = 'not installed'

        # aiohttp
        try:
            import aiohttp
            versions['aiohttp'] = aiohttp.__version__
        except ImportError:
            versions['aiohttp'] = 'not installed'

        # urllib3
        try:
            import urllib3
            versions['urllib3'] = urllib3.__version__
        except ImportError:
            versions['urllib3'] = 'not installed'

        # urllib (built-in, use Python version)
        versions['urllib'] = f'built-in (Python {sys.version.split()[0]})'

        # pycurl
        try:
            import pycurl
            versions['pycurl'] = pycurl.version
        except ImportError:
            versions['pycurl'] = 'not installed'

        # curl_cffi
        try:
            import curl_cffi
            versions['curl_cffi'] = curl_cffi.__version__ if hasattr(curl_cffi, '__version__') else 'unknown'
        except ImportError:
            versions['curl_cffi'] = 'not installed'

        return versions

    def run_all(self, lib_filter=None, generate_graphics=True):
        """Run all benchmarks

        Args:
            lib_filter: List of library names to test (None = all libraries)
            generate_graphics: Whether to generate graphics (default: True)
        """
        # Get library versions
        self.library_versions = self.get_library_versions()

        print("="*120)
        print("COMPREHENSIVE HTTP LIBRARY BENCHMARK")
        print("="*120)
        print(f"Sequential requests: {self.num_sequential} (warmup: {self.warmup})")
        print(f"Concurrent requests: {self.num_concurrent} (workers: {self.concurrent_workers})")
        if self.proxy_url_http or self.proxy_url_https:
            if self.proxy_url_http:
                proxy_display_http = self.proxy_url_http.split('@')[-1] if '@' in self.proxy_url_http else self.proxy_url_http
                print(f"Proxy HTTP configured: {proxy_display_http}")
            if self.proxy_url_https:
                proxy_display_https = self.proxy_url_https.split('@')[-1] if '@' in self.proxy_url_https else self.proxy_url_https
                print(f"Proxy HTTPS configured: {proxy_display_https}")
        else:
            print("Proxy: Not configured")

        if lib_filter:
            print(f"Testing only: {', '.join(lib_filter)}")

        print("-"*120)
        print("📚 Library Versions:")
        for lib_name in ['httpmorph', 'requests', 'httpx', 'aiohttp', 'urllib3', 'urllib', 'pycurl', 'curl_cffi']:
            version = self.library_versions.get(lib_name, 'unknown')
            status = "✓" if version not in ['not installed', 'unknown'] else "✗"
            print(f"  {status} {lib_name:<12} {version}")

        print("="*120)
        print()

        # Start local server
        print("Starting local HTTP server...")
        server = BenchmarkServer(self.local_port)
        server.start()

        # Run tests for each library
        for lib_name, lib_bench in self.lib_benchmarks.items():
            # Skip if library filter is specified and this library is not in it
            if lib_filter and lib_name not in lib_filter:
                continue

            # Check if library is available
            if not lib_bench.is_available():
                print(f"\n[{lib_name}] Not installed - SKIPPED")
                continue

            print(f"\n{'='*120}")
            print(f"Testing: {lib_name}")
            print('='*120)

            if lib_name not in self.results:
                self.results[lib_name] = {}

            # Get test matrix from library benchmark
            tests = lib_bench.get_test_matrix()

            for test_name, test_key in tests:
                # Check if method exists
                if not hasattr(lib_bench, test_key):
                    continue

                try:
                    print(f"  {test_name:<30} ", end='', flush=True)
                    method = getattr(lib_bench, test_key)
                    result = method()

                    self.results[lib_name][test_key] = result

                    if 'error' in result:
                        print(f"ERROR: {result['error']}")
                    elif 'mean_ms' in result:
                        # Sequential result
                        print(f"{result['mean_ms']:>8.2f}ms (median: {result['median_ms']:.2f}ms)")
                    elif 'req_per_sec' in result:
                        # Concurrent result
                        print(f"{result['req_per_sec']:>8.2f} req/s (avg: {result['avg_ms']:.2f}ms)")
                    else:
                        print("OK")
                except Exception as e:
                    error_msg = f"{type(e).__name__}: {str(e)[:50]}"
                    self.results[lib_name][test_key] = {'error': error_msg}
                    print(f"ERROR: {error_msg}")

        server.stop()

        # Print summary
        self.print_summary()

        # Export results
        self.export_results(generate_graphics=generate_graphics)

        return self.results

    def print_summary(self):
        """Print comprehensive summary"""
        print("\n" + "="*120)
        print("SUMMARY - PERFORMANCE COMPARISON")
        print("="*120)

        # Sequential tests summary
        print("\n📊 SEQUENTIAL TESTS (Mean Response Time)")
        print("-"*120)
        print(f"{'Library':<15} {'Local':<12} {'HTTPS':<12} {'HTTP/2':<12} {'Proxy':<12}")
        print("-"*120)

        for lib in ['httpmorph', 'requests', 'httpx', 'urllib3']:
            if lib not in self.results:
                continue

            row = f"{lib:<15}"
            for key in ['seq_local', 'seq_https', 'seq_http2', 'seq_proxy']:
                if key in self.results[lib] and 'mean_ms' in self.results[lib][key]:
                    row += f" {self.results[lib][key]['mean_ms']:>9.2f}ms"
                else:
                    row += f" {'N/A':>11}"
            print(row)

        # Concurrent tests summary
        print("\n🚀 CONCURRENT TESTS (Throughput)")
        print("-"*120)
        print(f"{'Library':<15} {'Local':<15} {'HTTPS':<15} {'Proxy':<15}")
        print("-"*120)

        for lib in ['httpmorph', 'requests', 'httpx', 'urllib3']:
            if lib not in self.results:
                continue

            row = f"{lib:<15}"
            for key in ['conc_local', 'conc_https', 'conc_proxy']:
                if key in self.results[lib] and 'req_per_sec' in self.results[lib][key]:
                    row += f" {self.results[lib][key]['req_per_sec']:>9.2f} req/s"
                else:
                    row += f" {'N/A':>14}"
            print(row)

        # Async tests summary
        print("\n⚡ ASYNC TESTS (Throughput)")
        print("-"*120)
        print(f"{'Library':<15} {'Local':<15} {'HTTPS':<15} {'Proxy':<15}")
        print("-"*120)

        for lib in ['httpmorph', 'httpx', 'aiohttp']:
            if lib not in self.results:
                continue

            row = f"{lib:<15}"
            for key in ['async_local', 'async_https', 'async_proxy']:
                if key in self.results[lib] and 'req_per_sec' in self.results[lib][key]:
                    row += f" {self.results[lib][key]['req_per_sec']:>9.2f} req/s"
                else:
                    row += f" {'N/A':>14}"
            print(row)

        print("\n" + "="*120)

        # Winners per category
        print("\n🏆 WINNERS BY CATEGORY")
        print("-"*120)

        categories = {
            'Fastest Sequential HTTPS': ('seq_https', 'mean_ms', min),
            'Highest Concurrent Throughput': ('conc_https', 'req_per_sec', max),
            'Highest Async Throughput': ('async_https', 'req_per_sec', max),
            'Best Proxy Performance (Async)': ('async_proxy', 'req_per_sec', max),
        }

        for category_name, (key, metric, comp_func) in categories.items():
            values = {}
            for lib in self.results:
                if key in self.results[lib] and metric in self.results[lib][key]:
                    values[lib] = self.results[lib][key][metric]

            if values:
                winner_lib = comp_func(values.items(), key=lambda x: x[1])
                winner_value = winner_lib[1]
                unit = 'ms' if metric.endswith('_ms') else 'req/s'
                print(f"{category_name:<35}: {winner_lib[0]:<15} ({winner_value:.2f} {unit})")

        print("="*120)

    def export_results(self, generate_graphics=True):
        """Export results to JSON and Markdown

        Args:
            generate_graphics: Whether to generate graphics (default: True)
        """
        # Get version from pyproject.toml
        version = 'unknown'
        try:
            pyproject_path = Path(__file__).parent.parent / 'pyproject.toml'
            if pyproject_path.exists():
                with open(pyproject_path) as f:
                    for line in f:
                        if line.strip().startswith('version'):
                            version = line.split('=')[1].strip().strip('"\'')
                            break
        except Exception:
            pass

        # Create directory structure: results/<os>/<version>/
        os_name = platform.system().lower()
        results_dir = Path(__file__).parent / 'results'
        os_dir = results_dir / os_name
        version_dir = os_dir / version
        version_dir.mkdir(parents=True, exist_ok=True)

        # Use fixed filenames: benchmark.json / benchmark.md (no timestamp)
        json_file = version_dir / 'benchmark.json'
        md_file = version_dir / 'benchmark.md'

        # Load existing results if they exist (for merge during retest)
        existing_results = {}
        if json_file.exists():
            try:
                with open(json_file, 'r') as f:
                    existing_data = json.load(f)
                    existing_results = existing_data.get('results', {})
                print(f"\n📂 Loading existing results from {json_file}")
                print(f"   Found results for: {', '.join(existing_results.keys())}")
            except Exception as e:
                print(f"\n⚠️  Could not load existing results: {e}")

        # Merge existing results with new results (new results overwrite)
        merged_results = existing_results.copy()
        for lib_name, lib_data in self.results.items():
            if lib_name not in merged_results:
                merged_results[lib_name] = {}
            merged_results[lib_name].update(lib_data)

        # Collect system information
        system_info = get_system_info()

        # Prepare export data
        export_data = {
            'metadata': {
                'timestamp': datetime.now().isoformat(),
                'version': version,
                'num_sequential': self.num_sequential,
                'num_concurrent': self.num_concurrent,
                'concurrent_workers': self.concurrent_workers,
                'warmup_requests': self.warmup,
                'library_versions': self.library_versions,  # Add library versions
                **system_info,  # Add all system info
            },
            'results': merged_results
        }

        # Export JSON: results/<os>/<version>/benchmark.json
        with open(json_file, 'w') as f:
            json.dump(export_data, f, indent=2)

        # Export Markdown: results/<os>/<version>/benchmark.md
        self._export_markdown(md_file, export_data)

        # Generate graphics if matplotlib is available and requested
        if MATPLOTLIB_AVAILABLE and generate_graphics:
            graphics_dir = version_dir / 'graphics'
            graphics_dir.mkdir(exist_ok=True)
            # Use "latest" as identifier instead of timestamp
            self._generate_graphics(export_data, graphics_dir, "latest")
            print(f"\n📊 Graphics generated: {graphics_dir}/*.png")

        # Analyze within-benchmark trends (only for newly tested libraries)
        trend_data = self._analyze_trends()
        if trend_data:
            print("\n📈 Within-Benchmark Trends:")
            for lib_name, lib_trends in trend_data.items():
                print(f"  {lib_name}:")
                for test_name, trend in lib_trends.items():
                    print(f"    {test_name}: {trend}")

        print("\n✅ Results exported to:")
        print(f"   {json_file}")
        print(f"   {md_file}")

    def _analyze_trends(self) -> dict:
        """Analyze within-benchmark performance trends with detailed metrics"""
        trends = {}

        try:
            for lib_name, lib_results in self.results.items():
                lib_trends = {}

                for test_name, test_data in lib_results.items():
                    if 'error' in test_data:
                        continue

                    # Build detailed trend analysis
                    trend_info = []

                    # Linear trend slope
                    if 'trend_slope_ms_per_req' in test_data:
                        slope = test_data['trend_slope_ms_per_req']
                        if abs(slope) > 0.001:  # Significant trend
                            if slope > 0:
                                trend_info.append(f"↗ +{slope:.4f}ms/req")
                            else:
                                trend_info.append(f"↘ {slope:.4f}ms/req")
                        else:
                            trend_info.append(f"→ Stable ({slope:+.5f}ms/req)")

                    # Coefficient of variation (stability)
                    if 'cv_pct' in test_data:
                        cv = test_data['cv_pct']
                        if cv < 5:
                            stability = "Very Stable"
                        elif cv < 15:
                            stability = "Stable"
                        elif cv < 30:
                            stability = "Moderate"
                        else:
                            stability = "Variable"
                        trend_info.append(f"CV: {cv:.1f}% ({stability})")

                    # Quartile progression
                    if all(k in test_data for k in ['q1_avg_ms', 'q2_avg_ms', 'q3_avg_ms', 'q4_avg_ms']):
                        q1, q2, q3, q4 = test_data['q1_avg_ms'], test_data['q2_avg_ms'], test_data['q3_avg_ms'], test_data['q4_avg_ms']
                        trend_info.append(f"Q1→Q4: {q1:.2f}→{q2:.2f}→{q3:.2f}→{q4:.2f}ms")

                    # Percentiles for sequential tests
                    if 'p95_ms' in test_data and 'p99_ms' in test_data:
                        trend_info.append(f"P95: {test_data['p95_ms']:.2f}ms, P99: {test_data['p99_ms']:.2f}ms")

                    if trend_info:
                        lib_trends[test_name] = " | ".join(trend_info)

                if lib_trends:
                    trends[lib_name] = lib_trends

        except Exception as e:
            print(f"\n⚠️  Trend analysis failed: {e}")

        return trends

    def _generate_graphics(self, data: dict, graphics_dir: Path, timestamp: str):
        """Generate performance comparison graphics"""
        if not MATPLOTLIB_AVAILABLE:
            return

        results = data['results']

        # 1. Sequential Performance - All Scenarios
        self._plot_sequential_comparison(results, graphics_dir, timestamp)

        # 2. Concurrent Performance - All Scenarios
        self._plot_concurrent_comparison(results, graphics_dir, timestamp)

        # 3. Async Performance - All Scenarios
        self._plot_async_comparison(results, graphics_dir, timestamp)

        # 4. HTTP/2 Performance
        self._plot_http2_comparison(results, graphics_dir, timestamp)

        # 5. Stability Comparison (CV%)
        self._plot_stability_comparison(results, graphics_dir, timestamp)

        # 6. Trend Analysis (Slope)
        self._plot_trend_comparison(results, graphics_dir, timestamp)

        # 7. Proxy Performance
        self._plot_proxy_comparison(results, graphics_dir, timestamp)

        # 8. Performance Heatmap
        self._plot_performance_heatmap(results, graphics_dir, timestamp)

        # 9. Overall Speed Ranking
        self._plot_overall_ranking(results, graphics_dir, timestamp)

    def _plot_sequential_comparison(self, results: dict, graphics_dir: Path, timestamp: str):
        """Plot sequential request performance comparison - ALL scenarios"""
        all_libs = ['httpmorph', 'requests', 'httpx', 'urllib3', 'urllib', 'aiohttp', 'pycurl', 'curl_cffi']
        scenarios = [
            ('seq_local_http', 'Local HTTP', '#4CAF50'),
            ('seq_remote_http', 'Remote HTTP', '#2196F3'),
            ('seq_remote_https', 'Remote HTTPS', '#9C27B0'),
            ('seq_proxy_http', 'Proxy HTTP', '#FF9800'),
        ]

        libs = []
        data_by_scenario = {s[0]: [] for s in scenarios}

        for lib_name in all_libs:
            if lib_name not in results:
                continue

            has_data = False
            for scenario_key, _, _ in scenarios:
                val = results[lib_name].get(scenario_key, {}).get('mean_ms')
                if val and not results[lib_name].get(scenario_key, {}).get('error'):
                    has_data = True

            if has_data:
                libs.append(lib_name)
                for scenario_key, _, _ in scenarios:
                    val = results[lib_name].get(scenario_key, {}).get('mean_ms')
                    data_by_scenario[scenario_key].append(val if val else 0)

        if not libs:
            return

        fig, ax = plt.subplots(figsize=(14, 7))
        x = range(len(libs))
        width = 0.2

        for idx, (scenario_key, label, color) in enumerate(scenarios):
            offset = (idx - len(scenarios)/2 + 0.5) * width
            bars = ax.bar([i + offset for i in x], data_by_scenario[scenario_key],
                         width, label=label, color=color)

            # Add value labels on top of bars
            for bar in bars:
                height = bar.get_height()
                if height > 0:
                    ax.text(bar.get_x() + bar.get_width()/2., height,
                           f'{height:.1f}', ha='center', va='bottom', fontsize=7)

        ax.set_ylabel('Response Time (ms)', fontsize=11)
        ax.set_title('Sequential Performance - All Scenarios (Lower is Better)', fontsize=13, fontweight='bold')
        ax.set_xticks(x)
        ax.set_xticklabels(libs, rotation=45, ha='right')
        ax.legend(loc='upper left', fontsize=9)
        ax.grid(True, alpha=0.3, axis='y')

        plt.tight_layout()
        plt.savefig(graphics_dir / f'01_sequential_all_{timestamp}.png', dpi=150)
        plt.close()

    def _plot_concurrent_comparison(self, results: dict, graphics_dir: Path, timestamp: str):
        """Plot concurrent throughput comparison - ALL scenarios"""
        all_libs = ['httpmorph', 'requests', 'httpx', 'urllib3', 'urllib', 'aiohttp', 'pycurl', 'curl_cffi']
        scenarios = [
            ('conc_local_http', 'Local HTTP', '#4CAF50'),
            ('conc_remote_http', 'Remote HTTP', '#2196F3'),
            ('conc_remote_https', 'Remote HTTPS', '#9C27B0'),
            ('conc_proxy_https', 'Proxy HTTPS', '#FF9800'),
        ]

        libs = []
        data_by_scenario = {s[0]: [] for s in scenarios}

        for lib_name in all_libs:
            if lib_name not in results:
                continue

            has_data = False
            for scenario_key, _, _ in scenarios:
                val = results[lib_name].get(scenario_key, {}).get('req_per_sec')
                if val and not results[lib_name].get(scenario_key, {}).get('error'):
                    has_data = True

            if has_data:
                libs.append(lib_name)
                for scenario_key, _, _ in scenarios:
                    val = results[lib_name].get(scenario_key, {}).get('req_per_sec')
                    data_by_scenario[scenario_key].append(val if val else 0)

        if not libs:
            return

        fig, ax = plt.subplots(figsize=(14, 7))
        x = range(len(libs))
        width = 0.2

        for idx, (scenario_key, label, color) in enumerate(scenarios):
            offset = (idx - len(scenarios)/2 + 0.5) * width
            bars = ax.bar([i + offset for i in x], data_by_scenario[scenario_key],
                         width, label=label, color=color)

            # Add value labels
            for bar in bars:
                height = bar.get_height()
                if height > 0:
                    ax.text(bar.get_x() + bar.get_width()/2., height,
                           f'{height:.0f}', ha='center', va='bottom', fontsize=7)

        ax.set_ylabel('Throughput (req/s)', fontsize=11)
        ax.set_title('Concurrent Throughput - All Scenarios (Higher is Better)', fontsize=13, fontweight='bold')
        ax.set_xticks(x)
        ax.set_xticklabels(libs, rotation=45, ha='right')
        ax.legend(loc='upper left', fontsize=9)
        ax.grid(True, alpha=0.3, axis='y')

        plt.tight_layout()
        plt.savefig(graphics_dir / f'02_concurrent_all_{timestamp}.png', dpi=150)
        plt.close()

    def _plot_async_comparison(self, results: dict, graphics_dir: Path, timestamp: str):
        """Plot async throughput comparison - ALL scenarios"""
        async_libs = ['httpmorph', 'httpx', 'aiohttp', 'curl_cffi']
        scenarios = [
            ('async_local_http', 'Local HTTP', '#4CAF50'),
            ('async_remote_http', 'Remote HTTP', '#2196F3'),
            ('async_remote_https', 'Remote HTTPS', '#9C27B0'),
            ('async_proxy_https', 'Proxy HTTPS', '#FF9800'),
            ('async_remote_http2', 'HTTP/2', '#00BCD4'),
        ]

        libs = []
        data_by_scenario = {s[0]: [] for s in scenarios}

        for lib_name in async_libs:
            if lib_name not in results:
                continue

            has_data = False
            for scenario_key, _, _ in scenarios:
                val = results[lib_name].get(scenario_key, {}).get('req_per_sec')
                if val and not results[lib_name].get(scenario_key, {}).get('error'):
                    has_data = True

            if has_data:
                libs.append(lib_name)
                for scenario_key, _, _ in scenarios:
                    val = results[lib_name].get(scenario_key, {}).get('req_per_sec')
                    data_by_scenario[scenario_key].append(val if val else 0)

        if not libs:
            return

        fig, ax = plt.subplots(figsize=(14, 7))
        x = range(len(libs))
        width = 0.15

        for idx, (scenario_key, label, color) in enumerate(scenarios):
            offset = (idx - len(scenarios)/2 + 0.5) * width
            bars = ax.bar([i + offset for i in x], data_by_scenario[scenario_key],
                         width, label=label, color=color)

            # Add value labels
            for bar in bars:
                height = bar.get_height()
                if height > 0:
                    ax.text(bar.get_x() + bar.get_width()/2., height,
                           f'{height:.0f}', ha='center', va='bottom', fontsize=7, rotation=90)

        ax.set_ylabel('Throughput (req/s)', fontsize=11)
        ax.set_title('Async Performance - All Scenarios (Higher is Better)', fontsize=13, fontweight='bold')
        ax.set_xticks(x)
        ax.set_xticklabels(libs, rotation=45, ha='right')
        ax.legend(loc='upper left', fontsize=9, ncol=2)
        ax.grid(True, alpha=0.3, axis='y')

        plt.tight_layout()
        plt.savefig(graphics_dir / f'03_async_all_{timestamp}.png', dpi=150)
        plt.close()

    def _plot_http2_comparison(self, results: dict, graphics_dir: Path, timestamp: str):
        """Plot HTTP/2 performance comparison"""
        libs = []
        seq_times = []
        conc_throughputs = []

        for lib_name in ['httpmorph', 'httpx', 'pycurl', 'curl_cffi']:
            if lib_name not in results:
                continue

            seq_val = results[lib_name].get('seq_remote_http2', {}).get('mean_ms')
            conc_val = results[lib_name].get('conc_remote_http2', {}).get('req_per_sec')

            if seq_val or conc_val:
                libs.append(lib_name)
                seq_times.append(seq_val if seq_val else 0)
                conc_throughputs.append(conc_val if conc_val else 0)

        if not libs:
            return

        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5))

        # Sequential HTTP/2
        ax1.bar(libs, seq_times, color='#00BCD4')
        ax1.set_ylabel('Response Time (ms)')
        ax1.set_title('HTTP/2 Sequential Performance (Lower is Better)')
        ax1.set_xticklabels(libs, rotation=45, ha='right')
        ax1.grid(True, alpha=0.3)

        # Concurrent HTTP/2
        ax2.bar(libs, conc_throughputs, color='#009688')
        ax2.set_ylabel('Throughput (req/s)')
        ax2.set_title('HTTP/2 Concurrent Throughput (Higher is Better)')
        ax2.set_xticklabels(libs, rotation=45, ha='right')
        ax2.grid(True, alpha=0.3)

        plt.tight_layout()
        plt.savefig(graphics_dir / f'04_http2_{timestamp}.png', dpi=150)
        plt.close()

    def _plot_time_series(self, results: dict, graphics_dir: Path, timestamp: str):
        """Plot time-series progression for httpmorph tests"""
        try:
            if 'httpmorph' not in results:
                return

            httpmorph_results = results['httpmorph']

            # Find tests with timing data
            test_plots = []
            for test_name, test_data in httpmorph_results.items():
                if 'error' in test_data:
                    continue

                timings = test_data.get('timings') or test_data.get('completion_times')
                if timings and len(timings) > 5:  # Only plot if we have enough data
                    test_plots.append((test_name, timings))

            if not test_plots:
                return

            # Plot up to 4 most interesting tests
            test_plots = test_plots[:4]
            n_plots = len(test_plots)

            if n_plots == 1:
                fig, axes = plt.subplots(1, 1, figsize=(12, 4))
                axes = [axes]
            elif n_plots == 2:
                fig, axes = plt.subplots(1, 2, figsize=(14, 5))
            else:
                fig, axes = plt.subplots(2, 2, figsize=(14, 10))
                axes = axes.flatten()

            for idx, (test_name, timings) in enumerate(test_plots):
                ax = axes[idx]

                # Plot time series
                x = list(range(1, len(timings) + 1))
                ax.plot(x, timings, linewidth=1, alpha=0.7, color='#2196F3')

                # Add trend line
                import numpy as np
                z = np.polyfit(x, timings, 1)
                p = np.poly1d(z)
                ax.plot(x, p(x), "r--", alpha=0.8, linewidth=2, label=f'Trend ({z[0]:+.3f} ms/req)')

                # Add mean line
                mean_val = sum(timings) / len(timings)
                ax.axhline(y=mean_val, color='green', linestyle=':', alpha=0.5, label=f'Mean ({mean_val:.2f}ms)')

                ax.set_xlabel('Request Number')
                ax.set_ylabel('Response Time (ms)')
                ax.set_title(f'{test_name}')
                ax.grid(True, alpha=0.3)
                ax.legend(fontsize=8)

            plt.tight_layout()
            plt.savefig(graphics_dir / f'timeseries_{timestamp}.png', dpi=150)
            plt.close()

        except Exception as e:
            print(f"⚠️  Time-series plot failed: {e}")

    def _plot_stability_comparison(self, results: dict, graphics_dir: Path, timestamp: str):
        """Plot stability comparison (Coefficient of Variation %) across all libraries"""
        all_libs = ['httpmorph', 'requests', 'httpx', 'urllib3', 'urllib', 'aiohttp', 'pycurl', 'curl_cffi']
        test_keys = ['seq_local_http', 'seq_remote_https', 'conc_local_http', 'conc_remote_https']

        libs = []
        cv_data = {k: [] for k in test_keys}

        for lib_name in all_libs:
            if lib_name not in results:
                continue

            has_data = False
            for test_key in test_keys:
                cv = results[lib_name].get(test_key, {}).get('cv_pct')
                if cv is not None and not results[lib_name].get(test_key, {}).get('error'):
                    has_data = True

            if has_data:
                libs.append(lib_name)
                for test_key in test_keys:
                    cv = results[lib_name].get(test_key, {}).get('cv_pct')
                    cv_data[test_key].append(cv if cv else 0)

        if not libs:
            return

        fig, ax = plt.subplots(figsize=(14, 7))
        x = range(len(libs))
        width = 0.2

        colors = ['#4CAF50', '#2196F3', '#9C27B0', '#FF9800']
        labels = ['Seq Local', 'Seq HTTPS', 'Conc Local', 'Conc HTTPS']

        for idx, (test_key, label, color) in enumerate(zip(test_keys, labels, colors)):
            offset = (idx - len(test_keys)/2 + 0.5) * width
            bars = ax.bar([i + offset for i in x], cv_data[test_key],
                         width, label=label, color=color)

            for bar in bars:
                height = bar.get_height()
                if height > 0:
                    ax.text(bar.get_x() + bar.get_width()/2., height,
                           f'{height:.1f}', ha='center', va='bottom', fontsize=7)

        ax.set_ylabel('Coefficient of Variation (%)', fontsize=11)
        ax.set_title('Stability Comparison (Lower is Better)', fontsize=13, fontweight='bold')
        ax.set_xticks(x)
        ax.set_xticklabels(libs, rotation=45, ha='right')
        ax.legend(fontsize=9)
        ax.grid(True, alpha=0.3, axis='y')
        ax.axhline(y=5, color='green', linestyle='--', alpha=0.5, label='Very Stable (< 5%)')
        ax.axhline(y=15, color='orange', linestyle='--', alpha=0.5, label='Moderate (< 15%)')

        plt.tight_layout()
        plt.savefig(graphics_dir / f'05_stability_{timestamp}.png', dpi=150)
        plt.close()

    def _plot_trend_comparison(self, results: dict, graphics_dir: Path, timestamp: str):
        """Plot trend slope comparison (ms per request) across all libraries"""
        all_libs = ['httpmorph', 'requests', 'httpx', 'urllib3', 'urllib', 'aiohttp', 'pycurl', 'curl_cffi']
        test_keys = ['seq_local_http', 'seq_remote_https', 'conc_local_http', 'conc_remote_https']

        libs = []
        trend_data = {k: [] for k in test_keys}

        for lib_name in all_libs:
            if lib_name not in results:
                continue

            has_data = False
            for test_key in test_keys:
                slope = results[lib_name].get(test_key, {}).get('trend_slope_ms_per_req')
                if slope is not None and not results[lib_name].get(test_key, {}).get('error'):
                    has_data = True

            if has_data:
                libs.append(lib_name)
                for test_key in test_keys:
                    slope = results[lib_name].get(test_key, {}).get('trend_slope_ms_per_req')
                    trend_data[test_key].append(slope if slope else 0)

        if not libs:
            return

        fig, ax = plt.subplots(figsize=(14, 7))
        x = range(len(libs))
        width = 0.2

        colors = ['#4CAF50', '#2196F3', '#9C27B0', '#FF9800']
        labels = ['Seq Local', 'Seq HTTPS', 'Conc Local', 'Conc HTTPS']

        for idx, (test_key, label, color) in enumerate(zip(test_keys, labels, colors)):
            offset = (idx - len(test_keys)/2 + 0.5) * width
            bars = ax.bar([i + offset for i in x], trend_data[test_key],
                         width, label=label, color=color)

            for bar in bars:
                height = bar.get_height()
                if abs(height) > 0.001:
                    ax.text(bar.get_x() + bar.get_width()/2., height,
                           f'{height:+.3f}', ha='center', va='bottom' if height > 0 else 'top',
                           fontsize=6, rotation=90)

        ax.set_ylabel('Trend Slope (ms/req)', fontsize=11)
        ax.set_title('Performance Trend Analysis (Closer to 0 is Better)', fontsize=13, fontweight='bold')
        ax.set_xticks(x)
        ax.set_xticklabels(libs, rotation=45, ha='right')
        ax.legend(fontsize=9)
        ax.grid(True, alpha=0.3, axis='y')
        ax.axhline(y=0, color='green', linestyle='-', alpha=0.7, linewidth=2)

        plt.tight_layout()
        plt.savefig(graphics_dir / f'06_trends_{timestamp}.png', dpi=150)
        plt.close()

    def _plot_proxy_comparison(self, results: dict, graphics_dir: Path, timestamp: str):
        """Plot proxy performance comparison across all libraries"""
        all_libs = ['httpmorph', 'requests', 'httpx', 'urllib3', 'urllib', 'aiohttp', 'pycurl', 'curl_cffi']

        libs = []
        seq_http = []
        seq_https = []
        conc_https = []
        async_https = []

        for lib_name in all_libs:
            if lib_name not in results:
                continue

            has_data = False
            s_http = results[lib_name].get('seq_proxy_http', {}).get('mean_ms')
            s_https = results[lib_name].get('seq_proxy_https', {}).get('mean_ms')
            c_https = results[lib_name].get('conc_proxy_https', {}).get('req_per_sec')
            a_https = results[lib_name].get('async_proxy_https', {}).get('req_per_sec')

            if any([s_http, s_https, c_https, a_https]):
                has_data = True

            if has_data:
                libs.append(lib_name)
                seq_http.append(s_http if s_http else 0)
                seq_https.append(s_https if s_https else 0)
                conc_https.append(c_https if c_https else 0)
                async_https.append(a_https if a_https else 0)

        if not libs:
            return

        fig, ((ax1, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(16, 12))

        # Sequential HTTP
        ax1.barh(libs, seq_http, color='#4CAF50')
        ax1.set_xlabel('Response Time (ms)')
        ax1.set_title('Proxy Sequential HTTP (Lower is Better)')
        ax1.grid(True, alpha=0.3, axis='x')

        # Sequential HTTPS
        ax2.barh(libs, seq_https, color='#2196F3')
        ax2.set_xlabel('Response Time (ms)')
        ax2.set_title('Proxy Sequential HTTPS (Lower is Better)')
        ax2.grid(True, alpha=0.3, axis='x')

        # Concurrent HTTPS
        ax3.barh(libs, conc_https, color='#9C27B0')
        ax3.set_xlabel('Throughput (req/s)')
        ax3.set_title('Proxy Concurrent HTTPS (Higher is Better)')
        ax3.grid(True, alpha=0.3, axis='x')

        # Async HTTPS
        ax4.barh(libs, async_https, color='#FF9800')
        ax4.set_xlabel('Throughput (req/s)')
        ax4.set_title('Proxy Async HTTPS (Higher is Better)')
        ax4.grid(True, alpha=0.3, axis='x')

        plt.tight_layout()
        plt.savefig(graphics_dir / f'07_proxy_{timestamp}.png', dpi=150)
        plt.close()

    def _plot_performance_heatmap(self, results: dict, graphics_dir: Path, timestamp: str):
        """Plot performance heatmap showing all tests for all libraries"""
        try:
            import numpy as np

            all_libs = ['httpmorph', 'requests', 'httpx', 'urllib3', 'urllib', 'aiohttp', 'pycurl', 'curl_cffi']
            test_categories = [
                'seq_local_http', 'seq_remote_http', 'seq_remote_https',
                'conc_local_http', 'conc_remote_http', 'conc_remote_https',
                'async_local_http', 'async_remote_http', 'async_remote_https',
            ]

            # Collect data
            libs = []
            data_matrix = []

            for lib_name in all_libs:
                if lib_name not in results:
                    continue

                row = []
                has_any_data = False

                for test_key in test_categories:
                    test_data = results[lib_name].get(test_key, {})
                    if 'error' in test_data or not test_data:
                        row.append(np.nan)
                    else:
                        # Normalize: use mean_ms for sequential, req_per_sec for concurrent/async
                        if test_key.startswith('seq'):
                            val = test_data.get('mean_ms')
                        else:
                            val = test_data.get('req_per_sec')

                        if val:
                            row.append(val)
                            has_any_data = True
                        else:
                            row.append(np.nan)

                if has_any_data:
                    libs.append(lib_name)
                    data_matrix.append(row)

            if not libs:
                return

            data_matrix = np.array(data_matrix)

            # Normalize each column (test) to 0-100 scale for better visualization
            normalized_matrix = np.zeros_like(data_matrix)
            for col_idx in range(data_matrix.shape[1]):
                col = data_matrix[:, col_idx]
                valid_vals = col[~np.isnan(col)]
                if len(valid_vals) > 0:
                    # For seq tests, lower is better, so invert
                    if test_categories[col_idx].startswith('seq'):
                        min_val, max_val = valid_vals.min(), valid_vals.max()
                        if max_val > min_val:
                            col_norm = 100 * (1 - (col - min_val) / (max_val - min_val))
                        else:
                            col_norm = np.full_like(col, 50)
                    else:
                        # For concurrent/async, higher is better
                        min_val, max_val = valid_vals.min(), valid_vals.max()
                        if max_val > min_val:
                            col_norm = 100 * (col - min_val) / (max_val - min_val)
                        else:
                            col_norm = np.full_like(col, 50)

                    normalized_matrix[:, col_idx] = col_norm

            # Create heatmap
            fig, ax = plt.subplots(figsize=(16, 8))
            im = ax.imshow(normalized_matrix, cmap='RdYlGn', aspect='auto', vmin=0, vmax=100)

            # Set ticks
            ax.set_xticks(range(len(test_categories)))
            ax.set_yticks(range(len(libs)))
            ax.set_xticklabels([t.replace('_', ' ').title() for t in test_categories], rotation=45, ha='right')
            ax.set_yticklabels(libs)

            # Add colorbar
            cbar = plt.colorbar(im, ax=ax)
            cbar.set_label('Relative Performance (0=Worst, 100=Best)', rotation=270, labelpad=20)

            # Add text annotations
            for i in range(len(libs)):
                for j in range(len(test_categories)):
                    val = normalized_matrix[i, j]
                    if not np.isnan(val):
                        text = ax.text(j, i, f'{val:.0f}',
                                     ha="center", va="center", color="black", fontsize=8)

            ax.set_title('Performance Heatmap - All Tests (Higher is Better)', fontsize=13, fontweight='bold')
            plt.tight_layout()
            plt.savefig(graphics_dir / f'08_heatmap_{timestamp}.png', dpi=150)
            plt.close()

        except Exception as e:
            print(f"⚠️  Heatmap plot failed: {e}")

    def _plot_overall_ranking(self, results: dict, graphics_dir: Path, timestamp: str):
        """Plot overall speed ranking across all libraries"""
        try:
            all_libs = ['httpmorph', 'requests', 'httpx', 'urllib3', 'urllib', 'aiohttp', 'pycurl', 'curl_cffi']

            # Calculate weighted score for each library
            scores = {}

            for lib_name in all_libs:
                if lib_name not in results:
                    continue

                lib_score = 0
                test_count = 0

                # Sequential tests (weight=1, lower is better)
                for test in ['seq_local_http', 'seq_remote_http', 'seq_remote_https']:
                    val = results[lib_name].get(test, {}).get('mean_ms')
                    if val and not results[lib_name].get(test, {}).get('error'):
                        lib_score += 1000 / val  # Invert so higher is better
                        test_count += 1

                # Concurrent tests (weight=2, higher is better)
                for test in ['conc_local_http', 'conc_remote_http', 'conc_remote_https']:
                    val = results[lib_name].get(test, {}).get('req_per_sec')
                    if val and not results[lib_name].get(test, {}).get('error'):
                        lib_score += val * 2
                        test_count += 2

                # Async tests (weight=2, higher is better)
                for test in ['async_local_http', 'async_remote_http', 'async_remote_https']:
                    val = results[lib_name].get(test, {}).get('req_per_sec')
                    if val and not results[lib_name].get(test, {}).get('error'):
                        lib_score += val * 2
                        test_count += 2

                if test_count > 0:
                    scores[lib_name] = lib_score / test_count

            if not scores:
                return

            # Sort by score
            sorted_libs = sorted(scores.items(), key=lambda x: x[1], reverse=True)
            libs = [lib for lib, _ in sorted_libs]
            values = [score for _, score in sorted_libs]

            # Create ranking plot
            fig, ax = plt.subplots(figsize=(12, 8))
            colors = plt.cm.viridis(np.linspace(0.3, 0.9, len(libs)))
            bars = ax.barh(libs, values, color=colors)

            # Add value labels and rank
            for idx, (bar, lib) in enumerate(zip(bars, libs)):
                width = bar.get_width()
                ax.text(width, bar.get_y() + bar.get_height()/2,
                       f'  #{idx+1}  Score: {width:.1f}',
                       ha='left', va='center', fontsize=10, fontweight='bold')

            ax.set_xlabel('Overall Performance Score (Higher is Better)', fontsize=11)
            ax.set_title('Overall Speed Ranking - Weighted Score Across All Tests', fontsize=13, fontweight='bold')
            ax.grid(True, alpha=0.3, axis='x')

            plt.tight_layout()
            plt.savefig(graphics_dir / f'09_ranking_{timestamp}.png', dpi=150)
            plt.close()

        except Exception as e:
            print(f"⚠️  Overall ranking plot failed: {e}")

    def _export_markdown(self, filepath: Path, data: dict):
        """Export results as comprehensive markdown report - fully dynamic"""
        md = []
        meta = data['metadata']
        results = data['results']

        # Header
        md.append("# httpmorph Benchmark Results\n\n")
        md.append(f"**Version:** {meta.get('version', 'unknown')} | ")
        md.append(f"**Generated:** {meta['timestamp'].split('T')[0]}\n\n")

        # System Information
        md.append("## System Information\n\n")
        md.append("| Property | Value |\n")
        md.append("|----------|-------|\n")
        md.append(f"| **OS** | {meta.get('os', 'unknown')} ({meta.get('platform', '')}) |\n")
        if 'processor' in meta:
            md.append(f"| **Processor** | {meta['processor']} |\n")
        if 'cpu_count' in meta:
            md.append(f"| **CPU Cores** | {meta['cpu_count']} |\n")
        if 'memory_gb' in meta:
            md.append(f"| **Memory** | {meta['memory_gb']} GB |\n")
        if 'python_version' in meta:
            md.append(f"| **Python** | {meta['python_version']} ({meta.get('python_implementation', 'CPython')}) |\n")
        md.append("\n")

        # Test Configuration
        md.append("## Test Configuration\n\n")
        md.append(f"- **Sequential Requests:** {meta['num_sequential']} (warmup: {meta.get('warmup_requests', 0)})\n")
        md.append(f"- **Concurrent Requests:** {meta['num_concurrent']} (workers: {meta['concurrent_workers']})\n\n")

        # Library Versions
        if 'library_versions' in meta:
            md.append("## Library Versions\n\n")
            md.append("| Library | Version | Status |\n")
            md.append("|---------|---------|--------|\n")
            lib_versions = meta['library_versions']
            for lib_name in ['httpmorph', 'requests', 'httpx', 'aiohttp', 'urllib3', 'urllib', 'pycurl', 'curl_cffi']:
                version = lib_versions.get(lib_name, 'unknown')
                status = "✅ Installed" if version not in ['not installed', 'unknown'] else "❌ Not Installed"
                md.append(f"| **{lib_name}** | `{version}` | {status} |\n")
            md.append("\n")

        # Helper function to get nice test name from key
        def format_test_name(key):
            """Convert test_key to readable name"""
            name = key.replace('_', ' ').title()
            # Fix common abbreviations
            name = name.replace('Http', 'HTTP')
            name = name.replace('Https', 'HTTPS')
            name = name.replace('Http2', 'HTTP/2')
            name = name.replace('Seq ', '')
            name = name.replace('Conc ', '')
            name = name.replace('Async ', '')
            return name

        # Dynamically discover all test keys across all libraries
        all_test_keys = set()
        for lib_results in results.values():
            all_test_keys.update(lib_results.keys())

        # Categorize tests by type
        seq_tests = sorted([k for k in all_test_keys if k.startswith('seq_')])
        conc_tests = sorted([k for k in all_test_keys if k.startswith('conc_')])
        async_tests = sorted([k for k in all_test_keys if k.startswith('async_')])

        # Get sorted list of libraries
        libraries = sorted(results.keys())

        # Sequential Tests
        if seq_tests:
            md.append("## Sequential Tests (Lower is Better)\n\n")
            md.append("Mean response time in milliseconds\n\n")

            # Build table header
            header = "| Library |"
            separator = "|---------|"
            for test_key in seq_tests:
                col_name = format_test_name(test_key)
                header += f" {col_name} |"
                separator += "--------:|"
            md.append(header + "\n")
            md.append(separator + "\n")

            # Build table rows
            for lib in libraries:
                # Check if library has any valid sequential data
                has_data = any(
                    test_key in results[lib] and 'mean_ms' in results[lib][test_key]
                    for test_key in seq_tests
                )
                if not has_data:
                    continue  # Skip libraries with no sequential data

                row = f"| **{lib}** |"
                for test_key in seq_tests:
                    if test_key in results[lib] and 'mean_ms' in results[lib][test_key]:
                        val = results[lib][test_key]['mean_ms']
                        row += f" {val:.2f}ms |"
                    elif test_key in results[lib] and 'error' in results[lib][test_key]:
                        row += " ERROR |"
                    else:
                        row += " N/A |"
                md.append(row + "\n")

            # Winners for sequential tests
            md.append("\n**Winners (Sequential):**\n")
            for test_key in seq_tests:
                values = {}
                for lib in libraries:
                    if test_key in results[lib] and 'mean_ms' in results[lib][test_key]:
                        values[lib] = results[lib][test_key]['mean_ms']
                if values:
                    winner = min(values.items(), key=lambda x: x[1])
                    test_name = format_test_name(test_key)
                    md.append(f"- {test_name}: **{winner[0]}** ({winner[1]:.2f}ms)\n")
            md.append("\n")

        # Concurrent Tests
        if conc_tests:
            md.append("## Concurrent Tests (Higher is Better)\n\n")
            md.append("Throughput in requests per second\n\n")

            # Build table header
            header = "| Library |"
            separator = "|---------|"
            for test_key in conc_tests:
                col_name = format_test_name(test_key)
                header += f" {col_name} |"
                separator += "--------:|"
            md.append(header + "\n")
            md.append(separator + "\n")

            # Build table rows
            for lib in libraries:
                # Check if library has any valid concurrent data
                has_data = any(
                    test_key in results[lib] and 'req_per_sec' in results[lib][test_key]
                    for test_key in conc_tests
                )
                if not has_data:
                    continue  # Skip libraries with no concurrent data

                row = f"| **{lib}** |"
                for test_key in conc_tests:
                    if test_key in results[lib] and 'req_per_sec' in results[lib][test_key]:
                        val = results[lib][test_key]['req_per_sec']
                        row += f" {val:.2f} |"
                    elif test_key in results[lib] and 'error' in results[lib][test_key]:
                        row += " ERROR |"
                    else:
                        row += " N/A |"
                md.append(row + "\n")

            # Winners for concurrent tests
            md.append("\n**Winners (Concurrent):**\n")
            for test_key in conc_tests:
                values = {}
                for lib in libraries:
                    if test_key in results[lib] and 'req_per_sec' in results[lib][test_key]:
                        values[lib] = results[lib][test_key]['req_per_sec']
                if values:
                    winner = max(values.items(), key=lambda x: x[1])
                    test_name = format_test_name(test_key)
                    md.append(f"- {test_name}: **{winner[0]}** ({winner[1]:.2f} req/s)\n")
            md.append("\n")

        # Async Tests
        if async_tests:
            md.append("## Async Tests (Higher is Better)\n\n")
            md.append("Throughput in requests per second\n\n")

            # Build table header
            header = "| Library |"
            separator = "|---------|"
            for test_key in async_tests:
                col_name = format_test_name(test_key)
                header += f" {col_name} |"
                separator += "--------:|"
            md.append(header + "\n")
            md.append(separator + "\n")

            # Build table rows
            for lib in libraries:
                # Check if library has any valid async data
                has_data = any(
                    test_key in results[lib] and 'req_per_sec' in results[lib][test_key]
                    for test_key in async_tests
                )
                if not has_data:
                    continue  # Skip libraries with no async data

                row = f"| **{lib}** |"
                for test_key in async_tests:
                    if test_key in results[lib] and 'req_per_sec' in results[lib][test_key]:
                        val = results[lib][test_key]['req_per_sec']
                        row += f" {val:.2f} |"
                    elif test_key in results[lib] and 'error' in results[lib][test_key]:
                        row += " ERROR |"
                    else:
                        row += " N/A |"
                md.append(row + "\n")

            # Winners for async tests
            md.append("\n**Winners (Async):**\n")
            for test_key in async_tests:
                values = {}
                for lib in libraries:
                    if test_key in results[lib] and 'req_per_sec' in results[lib][test_key]:
                        values[lib] = results[lib][test_key]['req_per_sec']
                if values:
                    winner = max(values.items(), key=lambda x: x[1])
                    test_name = format_test_name(test_key)
                    md.append(f"- {test_name}: **{winner[0]}** ({winner[1]:.2f} req/s)\n")
            md.append("\n")

        # Overall Performance Summary - httpmorph vs requests
        if 'httpmorph' in results and 'requests' in results:
            md.append("## Overall Performance Summary\n\n")
            md.append("### httpmorph vs requests Speedup\n\n")
            md.append("| Test | httpmorph | requests | Speedup |\n")
            md.append("|------|----------:|---------:|--------:|\n")

            # Find common tests between httpmorph and requests
            common_tests = set(results['httpmorph'].keys()) & set(results['requests'].keys())

            for test_key in sorted(common_tests):
                hm_result = results['httpmorph'][test_key]
                req_result = results['requests'][test_key]

                # Sequential tests (mean_ms)
                if 'mean_ms' in hm_result and 'mean_ms' in req_result:
                    hm_val = hm_result['mean_ms']
                    req_val = req_result['mean_ms']
                    speedup = req_val / hm_val if hm_val > 0 else 0
                    speedup_str = f"**{speedup:.2f}x** faster" if speedup > 1 else f"{speedup:.2f}x slower"
                    test_name = format_test_name(test_key)
                    md.append(f"| {test_name} | {hm_val:.2f}ms | {req_val:.2f}ms | {speedup_str} |\n")

                # Concurrent/Async tests (req_per_sec)
                elif 'req_per_sec' in hm_result and 'req_per_sec' in req_result:
                    hm_val = hm_result['req_per_sec']
                    req_val = req_result['req_per_sec']
                    speedup = hm_val / req_val if req_val > 0 else 0
                    speedup_str = f"**{speedup:.2f}x** faster" if speedup > 1 else f"{speedup:.2f}x slower"
                    test_name = format_test_name(test_key)
                    md.append(f"| {test_name} | {hm_val:.2f} req/s | {req_val:.2f} req/s | {speedup_str} |\n")

            md.append("\n")

        md.append("---\n")
        md.append("*Generated by httpmorph benchmark suite*\n")

        with open(filepath, 'w') as f:
            f.writelines(md)


def main():
    import argparse

    parser = argparse.ArgumentParser(description='Comprehensive HTTP library benchmark')
    parser.add_argument('-s', '--sequential', type=int, default=50,
                       help='Number of sequential requests (default: 50)')
    parser.add_argument('-c', '--concurrent', type=int, default=25,
                       help='Number of concurrent requests (default: 25)')
    parser.add_argument('-w', '--workers', type=int, default=10,
                       help='Number of concurrent workers (default: 10)')
    parser.add_argument('--warmup', type=int, default=5,
                       help='Number of warmup requests (default: 5)')
    parser.add_argument('--libraries', type=str, default=None,
                       help='Comma-separated list of libraries to test (e.g., httpmorph,requests,httpx). '
                            'If not specified, all available libraries will be tested.')
    parser.add_argument('--no-graphics', action='store_true',
                       help='Skip graphics generation')

    args = parser.parse_args()

    # Parse libraries filter
    lib_filter = None
    if args.libraries:
        lib_filter = [lib.strip() for lib in args.libraries.split(',')]

    # Disable warnings
    try:
        import urllib3
        urllib3.disable_warnings()
    except Exception:
        pass

    try:
        import warnings
        warnings.filterwarnings('ignore')
    except Exception:
        pass

    bench = Benchmark(
        num_sequential=args.sequential,
        warmup=args.warmup,
        num_concurrent=args.concurrent,
        concurrent_workers=args.workers
    )

    results = bench.run_all(lib_filter=lib_filter, generate_graphics=not args.no_graphics)
    return results


if __name__ == '__main__':
    main()
