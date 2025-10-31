"""Test concurrent async HTTP requests on Windows"""
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

async def fetch(client, url, req_id):
    """Fetch a single URL"""
    try:
        print(f"[Request {req_id}] Starting: {url}")
        start = time.time()
        response = await client.get(url, timeout=10.0)
        elapsed = time.time() - start
        print(f"[Request {req_id}] SUCCESS: status={response.status_code}, "
              f"size={len(response.body)}, time={elapsed:.2f}s")
        return req_id, response.status_code, len(response.body)
    except Exception as e:
        print(f"[Request {req_id}] ERROR: {e}")
        return req_id, None, None

async def main():
    async with AsyncClient() as client:
        print("AsyncClient created\n")

        # Test concurrent requests
        urls = [
            'http://httpbin.org/delay/1',
            'http://httpbin.org/delay/1',
            'http://httpbin.org/get',
            'http://httpbin.org/user-agent',
            'http://httpbin.org/headers',
        ]

        print(f"Making {len(urls)} concurrent requests...")
        start_time = time.time()

        tasks = [fetch(client, url, i+1) for i, url in enumerate(urls)]
        results = await asyncio.gather(*tasks)

        elapsed = time.time() - start_time

        print(f"\n{'='*60}")
        print(f"All requests completed in {elapsed:.2f}s")
        successful = sum(1 for _, status, _ in results if status == 200)
        print(f"Successful: {successful}/{len(urls)}")
        print(f"{'='*60}")

        return 0 if successful == len(urls) else 1

if __name__ == '__main__':
    exit_code = asyncio.run(main())
    sys.exit(exit_code)
