#!/usr/bin/env python3
"""
Comprehensive HTTP library benchmark comparing:
- httpmorph
- requests
- httpx
- aiohttp
- urllib3
- urllib

Tests cover: local HTTP, remote HTTP, remote HTTPS, HTTP/1.1, and HTTP/2
"""

import asyncio
import json
import os
import statistics
import threading
import time
from datetime import datetime
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from typing import Callable, Dict, List

import matplotlib.pyplot as plt
import numpy as np

# Import libraries (with graceful fallback)
libraries = {}

try:
    import httpmorph
    libraries['httpmorph'] = httpmorph
except ImportError:
    print("Warning: httpmorph not available")

try:
    import requests
    libraries['requests'] = requests
except ImportError:
    print("Warning: requests not available")

try:
    import httpx
    libraries['httpx'] = httpx
except ImportError:
    print("Warning: httpx not available")

try:
    import aiohttp
    libraries['aiohttp'] = aiohttp
except ImportError:
    print("Warning: aiohttp not available")

try:
    import urllib3
    libraries['urllib3'] = urllib3
except ImportError:
    print("Warning: urllib3 not available")

try:
    import urllib.error
    import urllib.request

    libraries['urllib'] = urllib
except ImportError:
    print("Warning: urllib not available")


# Simple HTTP server for local testing
class SimpleHTTPHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header('Content-Type', 'text/plain')
        self.end_headers()
        self.wfile.write(b'Hello, World!')

    def log_message(self, format, *args):
        pass  # Suppress logs


class BenchmarkServer:
    def __init__(self, port=18888):
        self.port = port
        self.server = HTTPServer(('127.0.0.1', port), SimpleHTTPHandler)
        self.thread = None

    def start(self):
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        time.sleep(0.5)  # Give server time to start

    def stop(self):
        self.server.shutdown()
        if self.thread:
            self.thread.join(timeout=2)


class Benchmark:
    def __init__(self, num_requests=100, warmup=10):
        self.num_requests = num_requests
        self.warmup = warmup
        self.results = {}
        self.local_port = 18888
        self.local_url = f'http://127.0.0.1:{self.local_port}/'
        self.remote_http_url = 'http://example.com/'
        self.remote_https_url = 'https://www.example.com/'
        self.http2_url = 'https://www.google.com/'

    def measure_time(self, func: Callable, *args, **kwargs) -> float:
        """Measure execution time of a function"""
        start = time.perf_counter()
        func(*args, **kwargs)
        return time.perf_counter() - start

    def measure_async(self, coro_func: Callable, *args, **kwargs) -> float:
        """Measure execution time of an async function"""
        async def run():
            start = time.perf_counter()
            await coro_func(*args, **kwargs)
            return time.perf_counter() - start

        return asyncio.run(run())

    def run_benchmark(self, name: str, func: Callable, is_async=False) -> Dict[str, float]:
        """Run a benchmark and return timing statistics"""
        times = []

        # Warmup
        for _ in range(self.warmup):
            try:
                if is_async:
                    self.measure_async(func)
                else:
                    self.measure_time(func)
            except Exception as e:
                return {'error': str(e)}

        # Actual benchmark
        for _ in range(self.num_requests):
            try:
                if is_async:
                    elapsed = self.measure_async(func)
                else:
                    elapsed = self.measure_time(func)
                times.append(elapsed)
            except Exception as e:
                return {'error': str(e)}

        return {
            'mean': statistics.mean(times) * 1000,  # Convert to ms
            'median': statistics.median(times) * 1000,
            'min': min(times) * 1000,
            'max': max(times) * 1000,
            'stdev': statistics.stdev(times) * 1000 if len(times) > 1 else 0,
            'total': sum(times),
        }

    # httpmorph benchmarks
    def bench_httpmorph_local(self):
        def run():
            resp = httpmorph.get(self.local_url, verify=False)
            assert resp.status_code == 200
        return self.run_benchmark('httpmorph_local', run)

    def bench_httpmorph_remote_http(self):
        def run():
            resp = httpmorph.get(self.remote_http_url, verify=False)
            assert resp.status_code == 200
        return self.run_benchmark('httpmorph_remote_http', run)

    def bench_httpmorph_remote_https(self):
        def run():
            resp = httpmorph.get(self.remote_https_url, verify=False)
            assert resp.status_code == 200
        return self.run_benchmark('httpmorph_remote_https', run)

    def bench_httpmorph_http2(self):
        """httpmorph with http2=True flag (new feature)"""
        def run():
            client = httpmorph.Client(http2=True)
            resp = client.get(self.http2_url, verify=False)
            assert resp.status_code == 200
            assert resp.http_version == "2.0"  # Verify HTTP/2 is used
        return self.run_benchmark('httpmorph_http2', run)

    # requests benchmarks
    def bench_requests_local(self):
        def run():
            resp = requests.get(self.local_url, verify=False)
            assert resp.status_code == 200
        return self.run_benchmark('requests_local', run)

    def bench_requests_remote_http(self):
        def run():
            resp = requests.get(self.remote_http_url, verify=False)
            assert resp.status_code == 200
        return self.run_benchmark('requests_remote_http', run)

    def bench_requests_remote_https(self):
        def run():
            resp = requests.get(self.remote_https_url, verify=False)
            assert resp.status_code == 200
        return self.run_benchmark('requests_remote_https', run)

    # httpx benchmarks (sync)
    def bench_httpx_local(self):
        def run():
            with httpx.Client(http2=False, verify=False) as client:
                resp = client.get(self.local_url)
                assert resp.status_code == 200
        return self.run_benchmark('httpx_local', run)

    def bench_httpx_remote_http(self):
        def run():
            with httpx.Client(http2=False, verify=False) as client:
                resp = client.get(self.remote_http_url)
                assert resp.status_code == 200
        return self.run_benchmark('httpx_remote_http', run)

    def bench_httpx_remote_https(self):
        def run():
            with httpx.Client(http2=False, verify=False) as client:
                resp = client.get(self.remote_https_url)
                assert resp.status_code == 200
        return self.run_benchmark('httpx_remote_https', run)

    def bench_httpx_http2(self):
        def run():
            with httpx.Client(http2=True, verify=False) as client:
                resp = client.get(self.http2_url)
                assert resp.status_code == 200
        return self.run_benchmark('httpx_http2', run)

    # aiohttp benchmarks (async)
    def bench_aiohttp_local(self):
        async def run():
            async with aiohttp.ClientSession() as session:
                async with session.get(self.local_url, ssl=False) as resp:
                    assert resp.status == 200
                    await resp.read()
        return self.run_benchmark('aiohttp_local', run, is_async=True)

    def bench_aiohttp_remote_http(self):
        async def run():
            async with aiohttp.ClientSession() as session:
                async with session.get(self.remote_http_url, ssl=False) as resp:
                    assert resp.status == 200
                    await resp.read()
        return self.run_benchmark('aiohttp_remote_http', run, is_async=True)

    def bench_aiohttp_remote_https(self):
        async def run():
            async with aiohttp.ClientSession() as session:
                async with session.get(self.remote_https_url, ssl=False) as resp:
                    assert resp.status == 200
                    await resp.read()
        return self.run_benchmark('aiohttp_remote_https', run, is_async=True)

    # urllib3 benchmarks
    def bench_urllib3_local(self):
        def run():
            http = urllib3.PoolManager(cert_reqs='CERT_NONE')
            resp = http.request('GET', self.local_url)
            assert resp.status == 200
        return self.run_benchmark('urllib3_local', run)

    def bench_urllib3_remote_http(self):
        def run():
            http = urllib3.PoolManager(cert_reqs='CERT_NONE')
            resp = http.request('GET', self.remote_http_url)
            assert resp.status == 200
        return self.run_benchmark('urllib3_remote_http', run)

    def bench_urllib3_remote_https(self):
        def run():
            http = urllib3.PoolManager(cert_reqs='CERT_NONE')
            resp = http.request('GET', self.remote_https_url)
            assert resp.status == 200
        return self.run_benchmark('urllib3_remote_https', run)

    # urllib benchmarks
    def bench_urllib_local(self):
        def run():
            import ssl
            ctx = ssl.create_default_context()
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
            resp = urllib.request.urlopen(self.local_url, context=ctx)
            assert resp.status == 200
            resp.read()
        return self.run_benchmark('urllib_local', run)

    def bench_urllib_remote_http(self):
        def run():
            import ssl
            ctx = ssl.create_default_context()
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
            resp = urllib.request.urlopen(self.remote_http_url, context=ctx)
            assert resp.status == 200
            resp.read()
        return self.run_benchmark('urllib_remote_http', run)

    def bench_urllib_remote_https(self):
        def run():
            import ssl
            ctx = ssl.create_default_context()
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
            resp = urllib.request.urlopen(self.remote_https_url, context=ctx)
            assert resp.status == 200
            resp.read()
        return self.run_benchmark('urllib_remote_https', run)

    def run_all(self):
        """Run all benchmarks"""
        print("Starting comprehensive HTTP library benchmark")
        print(f"Requests per test: {self.num_requests}, Warmup: {self.warmup}\n")

        # Start local server
        print("Starting local HTTP server...")
        server = BenchmarkServer(self.local_port)
        server.start()

        test_scenarios = [
            ('Local HTTP', 'local'),
            ('Remote HTTP', 'remote_http'),
            ('Remote HTTPS', 'remote_https'),
            ('HTTP/2', 'http2'),
        ]

        library_names = ['httpmorph', 'requests', 'httpx', 'aiohttp', 'urllib3', 'urllib']

        results = {lib: {} for lib in library_names}

        for scenario_name, scenario_key in test_scenarios:
            print(f"\n{'='*80}")
            print(f"Testing: {scenario_name}")
            print('='*80)

            for lib in library_names:
                if lib not in libraries:
                    results[lib][scenario_key] = {'error': 'Not installed'}
                    print(f"{lib:15} - Not installed")
                    continue

                method_name = f'bench_{lib}_{scenario_key}'
                if not hasattr(self, method_name):
                    results[lib][scenario_key] = {'error': 'Not supported'}
                    print(f"{lib:15} - Not supported")
                    continue

                try:
                    print(f"{lib:15} - Running...", end='', flush=True)
                    method = getattr(self, method_name)
                    result = method()
                    results[lib][scenario_key] = result

                    if 'error' in result:
                        print(f" ERROR: {result['error']}")
                    else:
                        print(f" {result['mean']:.2f}ms (median: {result['median']:.2f}ms)")
                except Exception as e:
                    error_msg = f"{type(e).__name__}: {str(e)}"
                    results[lib][scenario_key] = {'error': error_msg}
                    print(f" ERROR: {error_msg}")

        server.stop()

        # Print summary table
        self.print_summary(results, test_scenarios, library_names)

        return results, test_scenarios, library_names

    def print_summary(self, results: Dict, scenarios: List, libraries: List):
        """Print a nice summary table"""
        print("\n" + "="*120)
        print("SUMMARY - Mean Response Time (ms)")
        print("="*120)

        # Header
        header = f"{'Library':<15}"
        for scenario_name, _ in scenarios:
            header += f" | {scenario_name:<20}"
        print(header)
        print("-" * 120)

        # Data rows
        for lib in libraries:
            if lib not in results:
                continue

            row = f"{lib:<15}"
            for _, scenario_key in scenarios:
                if scenario_key not in results[lib]:
                    cell = "N/A"
                elif 'error' in results[lib][scenario_key]:
                    cell = "ERROR"
                else:
                    mean = results[lib][scenario_key]['mean']
                    cell = f"{mean:>6.2f}ms"
                row += f" | {cell:<20}"
            print(row)

        print("="*120)

        # Print fastest library per scenario
        print("\nFastest library per scenario:")
        print("-" * 60)

        for scenario_name, scenario_key in scenarios:
            times = {}
            for lib in libraries:
                if lib in results and scenario_key in results[lib]:
                    if 'error' not in results[lib][scenario_key]:
                        times[lib] = results[lib][scenario_key]['mean']

            if times:
                fastest = min(times.items(), key=lambda x: x[1])
                print(f"{scenario_name:<20}: {fastest[0]:<15} ({fastest[1]:.2f}ms)")
            else:
                print(f"{scenario_name:<20}: No valid results")

        print("="*120)

        # Calculate speedup vs requests (baseline)
        if 'requests' in results:
            print("\nSpeedup vs requests (baseline):")
            print("-" * 60)

            for scenario_name, scenario_key in scenarios:
                if scenario_key not in results.get('requests', {}):
                    continue
                if 'error' in results['requests'][scenario_key]:
                    continue

                baseline = results['requests'][scenario_key]['mean']
                print(f"\n{scenario_name}:")

                for lib in libraries:
                    if lib == 'requests':
                        continue
                    if lib not in results or scenario_key not in results[lib]:
                        continue
                    if 'error' in results[lib][scenario_key]:
                        continue

                    lib_time = results[lib][scenario_key]['mean']
                    speedup = baseline / lib_time
                    speedup_str = f"{speedup:.2f}x" if speedup > 1 else f"{1/speedup:.2f}x slower"
                    print(f"  {lib:<15}: {speedup_str}")

    def generate_visualizations(self, results: Dict, scenarios: List, libraries: List, output_dir: str = 'benchmarks/res'):
        """Generate and save visualization charts"""
        Path(output_dir).mkdir(parents=True, exist_ok=True)

        # 1. Comparison bar chart for all scenarios
        self._plot_comparison_chart(results, scenarios, libraries, output_dir)

        # 2. Speedup comparison vs requests
        self._plot_speedup_chart(results, scenarios, libraries, output_dir)

        # 3. Performance heatmap
        self._plot_heatmap(results, scenarios, libraries, output_dir)

        # 4. Individual scenario charts
        self._plot_individual_scenarios(results, scenarios, libraries, output_dir)

        # 5. Export to JSON
        self._export_json(results, scenarios, libraries, output_dir)

        # 6. Export to Markdown
        self._export_markdown(results, scenarios, libraries, output_dir)

        print(f"\nVisualizations and data files saved to: {output_dir}/")

    def _plot_comparison_chart(self, results: Dict, scenarios: List, libraries: List, output_dir: str):
        """Create grouped bar chart comparing all libraries across scenarios"""
        fig, ax = plt.subplots(figsize=(14, 8))

        scenario_names = [name for name, _ in scenarios]
        x = np.arange(len(scenario_names))
        width = 0.13  # Width of each bar

        colors = ['#2E86AB', '#A23B72', '#F18F01', '#C73E1D', '#6A994E', '#BC4B51']

        for i, lib in enumerate(libraries):
            if lib not in results:
                continue

            means = []
            for _, key in scenarios:
                if key in results[lib] and 'error' not in results[lib][key]:
                    means.append(results[lib][key]['mean'])
                else:
                    means.append(0)

            offset = width * (i - len(libraries) / 2)
            bars = ax.bar(x + offset, means, width, label=lib, color=colors[i % len(colors)])

            # Add value labels on bars
            for j, (bar, val) in enumerate(zip(bars, means)):
                if val > 0:
                    height = bar.get_height()
                    ax.text(bar.get_x() + bar.get_width() / 2., height,
                           f'{val:.1f}',
                           ha='center', va='bottom', fontsize=7, rotation=90)

        ax.set_xlabel('Test Scenario', fontsize=12, fontweight='bold')
        ax.set_ylabel('Response Time (ms)', fontsize=12, fontweight='bold')
        ax.set_title('HTTP Library Performance Comparison\nLower is Better', fontsize=14, fontweight='bold')
        ax.set_xticks(x)
        ax.set_xticklabels(scenario_names)
        ax.legend(loc='upper left', ncol=2)
        ax.grid(axis='y', alpha=0.3)

        plt.tight_layout()
        plt.savefig(f'{output_dir}/benchmark_comparison_all_scenarios.png', dpi=300, bbox_inches='tight')
        plt.close()

    def _plot_speedup_chart(self, results: Dict, scenarios: List, libraries: List, output_dir: str):
        """Create speedup comparison chart vs requests baseline"""
        if 'requests' not in results:
            return

        fig, ax = plt.subplots(figsize=(14, 8))

        scenario_names = [name for name, _ in scenarios]
        x = np.arange(len(scenario_names))
        width = 0.15

        colors = ['#2E86AB', '#F18F01', '#C73E1D', '#6A994E', '#BC4B51']

        lib_index = 0
        for lib in libraries:
            if lib == 'requests' or lib not in results:
                continue

            speedups = []
            for _, key in scenarios:
                if key not in results.get('requests', {}) or 'error' in results['requests'].get(key, {}):
                    speedups.append(0)
                    continue

                if key in results[lib] and 'error' not in results[lib][key]:
                    baseline = results['requests'][key]['mean']
                    lib_time = results[lib][key]['mean']
                    speedup = baseline / lib_time
                    speedups.append(speedup)
                else:
                    speedups.append(0)

            offset = width * (lib_index - (len(libraries) - 2) / 2)
            bars = ax.bar(x + offset, speedups, width, label=lib, color=colors[lib_index % len(colors)])

            # Add value labels
            for bar, val in zip(bars, speedups):
                if val > 0:
                    height = bar.get_height()
                    ax.text(bar.get_x() + bar.get_width() / 2., height,
                           f'{val:.2f}x',
                           ha='center', va='bottom', fontsize=8)

            lib_index += 1

        ax.axhline(y=1.0, color='red', linestyle='--', linewidth=2, label='requests baseline', alpha=0.7)
        ax.set_xlabel('Test Scenario', fontsize=12, fontweight='bold')
        ax.set_ylabel('Speedup Factor', fontsize=12, fontweight='bold')
        ax.set_title('Speedup vs requests (baseline)\nHigher is Better', fontsize=14, fontweight='bold')
        ax.set_xticks(x)
        ax.set_xticklabels(scenario_names)
        ax.legend(loc='upper left', ncol=2)
        ax.grid(axis='y', alpha=0.3)

        plt.tight_layout()
        plt.savefig(f'{output_dir}/benchmark_speedup_comparison.png', dpi=300, bbox_inches='tight')
        plt.close()

    def _plot_heatmap(self, results: Dict, scenarios: List, libraries: List, output_dir: str):
        """Create a heatmap showing performance across all libraries and scenarios"""
        scenario_names = [name for name, _ in scenarios]

        # Create data matrix
        data = []
        libs_with_data = []

        for lib in libraries:
            if lib not in results:
                continue

            row = []
            has_data = False
            for _, key in scenarios:
                if key in results[lib] and 'error' not in results[lib][key]:
                    row.append(results[lib][key]['mean'])
                    has_data = True
                else:
                    row.append(np.nan)

            if has_data:
                data.append(row)
                libs_with_data.append(lib)

        if not data:
            return

        data = np.array(data)

        fig, ax = plt.subplots(figsize=(12, 6))
        im = ax.imshow(data, cmap='RdYlGn_r', aspect='auto')

        # Set ticks
        ax.set_xticks(np.arange(len(scenario_names)))
        ax.set_yticks(np.arange(len(libs_with_data)))
        ax.set_xticklabels(scenario_names)
        ax.set_yticklabels(libs_with_data)

        # Rotate the tick labels
        plt.setp(ax.get_xticklabels(), rotation=45, ha="right", rotation_mode="anchor")

        # Add text annotations
        for i in range(len(libs_with_data)):
            for j in range(len(scenario_names)):
                if not np.isnan(data[i, j]):
                    ax.text(j, i, f'{data[i, j]:.1f}',
                           ha="center", va="center", color="black", fontsize=10, fontweight='bold')

        ax.set_title('Performance Heatmap (ms)\nLower is Better', fontsize=14, fontweight='bold')
        fig.colorbar(im, ax=ax, label='Response Time (ms)')

        plt.tight_layout()
        plt.savefig(f'{output_dir}/benchmark_performance_heatmap.png', dpi=300, bbox_inches='tight')
        plt.close()

    def _plot_individual_scenarios(self, results: Dict, scenarios: List, libraries: List, output_dir: str):
        """Create individual bar charts for each scenario"""
        for scenario_name, scenario_key in scenarios:
            fig, ax = plt.subplots(figsize=(10, 6))

            libs = []
            times = []
            colors_list = []
            colors = ['#2E86AB', '#A23B72', '#F18F01', '#C73E1D', '#6A994E', '#BC4B51']

            for i, lib in enumerate(libraries):
                if lib not in results or scenario_key not in results[lib]:
                    continue
                if 'error' in results[lib][scenario_key]:
                    continue

                libs.append(lib)
                times.append(results[lib][scenario_key]['mean'])
                colors_list.append(colors[i % len(colors)])

            if not libs:
                plt.close()
                continue

            # Sort by time
            sorted_data = sorted(zip(libs, times, colors_list), key=lambda x: x[1])
            libs, times, colors_list = zip(*sorted_data)

            bars = ax.barh(libs, times, color=colors_list)

            # Add value labels
            for bar, val in zip(bars, times):
                width = bar.get_width()
                ax.text(width, bar.get_y() + bar.get_height() / 2.,
                       f' {val:.2f}ms',
                       ha='left', va='center', fontsize=10, fontweight='bold')

            ax.set_xlabel('Response Time (ms)', fontsize=12, fontweight='bold')
            ax.set_title(f'{scenario_name} Performance\nLower is Better', fontsize=14, fontweight='bold')
            ax.grid(axis='x', alpha=0.3)

            # Highlight the fastest
            if bars:
                bars[0].set_edgecolor('gold')
                bars[0].set_linewidth(3)

            plt.tight_layout()
            safe_name = scenario_name.lower().replace(' ', '_').replace('/', '_')
            plt.savefig(f'{output_dir}/benchmark_scenario_{safe_name}.png', dpi=300, bbox_inches='tight')
            plt.close()

    def _export_json(self, results: Dict, scenarios: List, libraries: List, output_dir: str):
        """Export results to JSON file"""
        timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')

        export_data = {
            'metadata': {
                'timestamp': datetime.now().isoformat(),
                'num_requests': self.num_requests,
                'warmup': self.warmup,
                'scenarios': [{'name': name, 'key': key} for name, key in scenarios],
                'libraries': libraries
            },
            'results': {}
        }

        # Organize results by library
        for lib in libraries:
            if lib not in results:
                continue

            export_data['results'][lib] = {}
            for scenario_name, scenario_key in scenarios:
                if scenario_key in results[lib]:
                    export_data['results'][lib][scenario_name] = results[lib][scenario_key]

        # Save with timestamp
        json_file = f'{output_dir}/benchmark_results_{timestamp}.json'
        with open(json_file, 'w') as f:
            json.dump(export_data, f, indent=2)

        # Also save as latest
        latest_file = f'{output_dir}/benchmark_results_latest.json'
        with open(latest_file, 'w') as f:
            json.dump(export_data, f, indent=2)

    def _export_markdown(self, results: Dict, scenarios: List, libraries: List, output_dir: str):
        """Export results as Markdown tables"""
        timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        scenario_names = [name for name, _ in scenarios]

        md_content = []
        md_content.append("# HTTP Library Benchmark Results\n")
        md_content.append(f"**Generated:** {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        md_content.append(f"**Requests per test:** {self.num_requests}\n")
        md_content.append(f"**Warmup requests:** {self.warmup}\n\n")

        # Summary table
        md_content.append("## Performance Summary (Mean Response Time in ms)\n\n")

        # Table header
        header = "| Library |"
        separator = "|---------|"
        for name in scenario_names:
            header += f" {name} |"
            separator += "-------:|"
        md_content.append(header + "\n")
        md_content.append(separator + "\n")

        # Table rows
        for lib in libraries:
            if lib not in results:
                continue

            row = f"| **{lib}** |"
            for _, key in scenarios:
                if key in results[lib] and 'error' not in results[lib][key]:
                    mean = results[lib][key]['mean']
                    row += f" {mean:.2f} |"
                else:
                    row += " N/A |"
            md_content.append(row + "\n")

        md_content.append("\n")

        # Detailed statistics for each scenario
        md_content.append("## Detailed Statistics\n\n")

        for scenario_name, scenario_key in scenarios:
            md_content.append(f"### {scenario_name}\n\n")
            md_content.append("| Library | Mean (ms) | Median (ms) | Min (ms) | Max (ms) | Std Dev (ms) |\n")
            md_content.append("|---------|----------:|-----------:|---------:|---------:|-------------:|\n")

            # Collect data and sort by mean
            lib_stats = []
            for lib in libraries:
                if lib not in results or scenario_key not in results[lib]:
                    continue
                if 'error' in results[lib][scenario_key]:
                    continue

                stats = results[lib][scenario_key]
                lib_stats.append((lib, stats))

            # Sort by mean time
            lib_stats.sort(key=lambda x: x[1]['mean'])

            # Add rows
            for i, (lib, stats) in enumerate(lib_stats):
                # Highlight fastest with emoji
                lib_name = f"🥇 **{lib}**" if i == 0 else f"**{lib}**"
                md_content.append(
                    f"| {lib_name} | {stats['mean']:.2f} | {stats['median']:.2f} | "
                    f"{stats['min']:.2f} | {stats['max']:.2f} | {stats['stdev']:.2f} |\n"
                )

            md_content.append("\n")

        # Speedup analysis vs requests
        if 'requests' in results:
            md_content.append("## Speedup vs requests (baseline)\n\n")
            md_content.append("| Scenario | httpmorph | httpx | aiohttp | urllib3 | urllib |\n")
            md_content.append("|----------|----------:|------:|--------:|--------:|-------:|\n")

            for scenario_name, scenario_key in scenarios:
                if scenario_key not in results.get('requests', {}) or 'error' in results['requests'].get(scenario_key, {}):
                    continue

                baseline = results['requests'][scenario_key]['mean']
                row = f"| **{scenario_name}** |"

                for lib in ['httpmorph', 'httpx', 'aiohttp', 'urllib3', 'urllib']:
                    if lib in results and scenario_key in results[lib] and 'error' not in results[lib][scenario_key]:
                        lib_time = results[lib][scenario_key]['mean']
                        speedup = baseline / lib_time
                        if speedup >= 1.0:
                            row += f" {speedup:.2f}x |"
                        else:
                            row += f" {1/speedup:.2f}x slower |"
                    else:
                        row += " N/A |"

                md_content.append(row + "\n")

            md_content.append("\n")

        # Key findings
        md_content.append("## Key Findings\n\n")
        md_content.append("### Fastest Library per Scenario\n\n")

        for scenario_name, scenario_key in scenarios:
            times = {}
            for lib in libraries:
                if lib in results and scenario_key in results[lib]:
                    if 'error' not in results[lib][scenario_key]:
                        times[lib] = results[lib][scenario_key]['mean']

            if times:
                fastest = min(times.items(), key=lambda x: x[1])
                md_content.append(f"- **{scenario_name}**: {fastest[0]} ({fastest[1]:.2f}ms)\n")

        md_content.append("\n")

        # Save with timestamp
        md_file = f'{output_dir}/benchmark_results_{timestamp}.md'
        with open(md_file, 'w') as f:
            f.writelines(md_content)

        # Also save as latest
        latest_file = f'{output_dir}/benchmark_results_latest.md'
        with open(latest_file, 'w') as f:
            f.writelines(md_content)


def main():
    import argparse

    parser = argparse.ArgumentParser(description='Comprehensive HTTP library benchmark')
    parser.add_argument('-n', '--num-requests', type=int, default=50,
                       help='Number of requests per test (default: 50)')
    parser.add_argument('-w', '--warmup', type=int, default=5,
                       help='Number of warmup requests (default: 5)')

    args = parser.parse_args()

    # Disable SSL warnings
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

    bench = Benchmark(num_requests=args.num_requests, warmup=args.warmup)
    results, scenarios, libraries = bench.run_all()

    # Generate visualizations
    print("\n" + "="*120)
    print("Generating visualizations...")
    print("="*120)
    bench.generate_visualizations(results, scenarios, libraries)

    return results


if __name__ == '__main__':
    main()
