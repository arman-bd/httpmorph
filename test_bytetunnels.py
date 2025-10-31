"""Test with httpmorph-bin.bytetunnels.com server"""
import asyncio
import sys
import os
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'src'))

from httpmorph import AsyncClient, HAS_ASYNC

async def test_single():
    """Test single request"""
    print("=" * 60)
    print("Test 1: Single request")
    print("=" * 60)
    async with AsyncClient() as client:
        try:
            start = time.time()
            response = await client.get('http://httpmorph-bin.bytetunnels.com/get', timeout=10.0)
            elapsed = time.time() - start
            print(f"SUCCESS: status={response.status_code}, size={len(response.body)}, time={elapsed:.2f}s\n")
            return True
        except Exception as e:
            print(f"ERROR: {e}\n")
            return False

async def test_sequential():
    """Test sequential requests"""
    print("=" * 60)
    print("Test 2: Sequential requests (3 requests)")
    print("=" * 60)
    async with AsyncClient() as client:
        urls = [
            'http://httpmorph-bin.bytetunnels.com/get',
            'http://httpmorph-bin.bytetunnels.com/user-agent',
            'http://httpmorph-bin.bytetunnels.com/headers',
        ]

        results = []
        for i, url in enumerate(urls, 1):
            try:
                print(f"[{i}/{len(urls)}] {url}")
                start = time.time()
                response = await client.get(url, timeout=10.0)
                elapsed = time.time() - start
                print(f"  SUCCESS: status={response.status_code}, size={len(response.body)}, time={elapsed:.2f}s")
                results.append(True)
            except Exception as e:
                print(f"  ERROR: {e}")
                results.append(False)

        successful = sum(results)
        print(f"\nResults: {successful}/{len(urls)} successful\n")
        return successful == len(urls)

async def test_concurrent():
    """Test concurrent requests"""
    print("=" * 60)
    print("Test 3: Concurrent requests (5 requests)")
    print("=" * 60)

    async def fetch(client, url, req_id):
        try:
            start = time.time()
            response = await client.get(url, timeout=10.0)
            elapsed = time.time() - start
            print(f"[Request {req_id}] SUCCESS: status={response.status_code}, size={len(response.body)}, time={elapsed:.2f}s")
            return True
        except Exception as e:
            print(f"[Request {req_id}] ERROR: {e}")
            return False

    async with AsyncClient() as client:
        urls = [
            'http://httpmorph-bin.bytetunnels.com/delay/1',
            'http://httpmorph-bin.bytetunnels.com/delay/1',
            'http://httpmorph-bin.bytetunnels.com/get',
            'http://httpmorph-bin.bytetunnels.com/user-agent',
            'http://httpmorph-bin.bytetunnels.com/headers',
        ]

        print(f"Making {len(urls)} concurrent requests...")
        start_time = time.time()

        tasks = [fetch(client, url, i+1) for i, url in enumerate(urls)]
        results = await asyncio.gather(*tasks)

        elapsed = time.time() - start_time
        successful = sum(results)

        print(f"\nAll requests completed in {elapsed:.2f}s")
        print(f"Successful: {successful}/{len(urls)}\n")
        return successful == len(urls)

async def main():
    print(f"HAS_ASYNC = {HAS_ASYNC}\n")

    results = []

    # Test 1: Single request
    results.append(await test_single())

    # Test 2: Sequential requests
    results.append(await test_sequential())

    # Test 3: Concurrent requests
    results.append(await test_concurrent())

    # Summary
    print("=" * 60)
    print("SUMMARY")
    print("=" * 60)
    print(f"Test 1 (Single):     {'PASS' if results[0] else 'FAIL'}")
    print(f"Test 2 (Sequential): {'PASS' if results[1] else 'FAIL'}")
    print(f"Test 3 (Concurrent): {'PASS' if results[2] else 'FAIL'}")
    print(f"\nOverall: {sum(results)}/3 tests passed")
    print("=" * 60)

    return 0 if all(results) else 1

if __name__ == '__main__':
    exit_code = asyncio.run(main())
    sys.exit(exit_code)
