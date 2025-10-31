"""Test sequential async HTTP requests on Windows"""
import asyncio
import sys
import os
import time

# Add src to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'src'))

try:
    from httpmorph import AsyncClient, HAS_ASYNC
    print(f"HAS_ASYNC = {HAS_ASYNC}")
except ImportError as e:
    print(f"Failed to import: {e}")
    sys.exit(1)

async def main():
    async with AsyncClient() as client:
        print("AsyncClient created\n")

        # Test sequential requests
        urls = [
            ('http://httpbin.org/get', 'Basic GET'),
            ('http://httpbin.org/user-agent', 'User-Agent'),
            ('http://httpbin.org/headers', 'Headers'),
        ]

        results = []
        for i, (url, desc) in enumerate(urls, 1):
            try:
                print(f"[{i}/{len(urls)}] {desc}: {url}")
                start = time.time()
                response = await client.get(url, timeout=10.0)
                elapsed = time.time() - start
                print(f"  SUCCESS: status={response.status_code}, size={len(response.body)}, time={elapsed:.2f}s\n")
                results.append(True)
            except Exception as e:
                print(f"  ERROR: {e}\n")
                results.append(False)

        successful = sum(results)
        print(f"{'='*60}")
        print(f"Results: {successful}/{len(urls)} successful")
        print(f"{'='*60}")

        return 0 if successful == len(urls) else 1

if __name__ == '__main__':
    exit_code = asyncio.run(main())
    sys.exit(exit_code)
