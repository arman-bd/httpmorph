"""
Example demonstrating AsyncClient with C-level async I/O

This example shows how to use httpmorph's true async I/O capabilities
without thread pool overhead.
"""

import asyncio

import httpmorph


async def main():
    """Main async function demonstrating AsyncClient"""

    print("=== httpmorph AsyncClient Demo ===\n")

    # Check if async bindings are available
    if not httpmorph.HAS_ASYNC:
        print("❌ Async bindings not available.")
        print("Please rebuild: python setup.py build_ext --inplace")
        return

    print("✅ Async bindings loaded successfully!\n")

    # Use AsyncClient with context manager
    async with httpmorph.AsyncClient() as client:
        print("📡 Making async GET request to https://httpbin.org/get...")

        try:
            # Make an async GET request
            response = await client.get("https://httpbin.org/get", timeout=10.0)

            print("\n✅ Response received!")
            print(f"   Status Code: {response.status_code}")
            print(f"   HTTP Version: {response.http_version}")
            print(f"   Total Time: {response.elapsed.total_seconds():.3f}s")

            # Show timing breakdown
            print("\n⏱️  Timing Breakdown:")
            print(f"   Connect: {response.connect_time_us / 1000:.2f}ms")
            print(f"   TLS: {response.tls_time_us / 1000:.2f}ms")
            print(f"   First Byte: {response.first_byte_time_us / 1000:.2f}ms")
            print(f"   Total: {response.total_time_us / 1000:.2f}ms")

            # Show TLS info
            if response.tls_version:
                print("\n🔒 TLS Info:")
                print(f"   Version: {response.tls_version}")
                print(f"   Cipher: {response.tls_cipher}")

            # Parse JSON response
            data = response.json()
            print("\n📊 Response Data:")
            print(f"   Origin: {data.get('origin', 'N/A')}")
            print(f"   Headers: {len(data.get('headers', {}))}")

        except asyncio.TimeoutError:
            print("\n❌ Request timed out")
        except Exception as e:
            print(f"\n❌ Error: {e}")

    print("\n=== Demo Complete ===")


async def concurrent_requests_demo():
    """Demo showing concurrent requests with AsyncClient"""

    print("\n=== Concurrent Requests Demo ===\n")

    if not httpmorph.HAS_ASYNC:
        return

    async with httpmorph.AsyncClient() as client:
        # Make multiple concurrent requests
        urls = [
            "https://httpbin.org/delay/1",
            "https://httpbin.org/delay/2",
            "https://httpbin.org/delay/1",
        ]

        print(f"🚀 Making {len(urls)} concurrent requests...")
        start_time = asyncio.get_event_loop().time()

        # Create tasks for all requests
        tasks = [client.get(url, timeout=10.0) for url in urls]

        # Wait for all to complete
        responses = await asyncio.gather(*tasks, return_exceptions=True)

        end_time = asyncio.get_event_loop().time()
        total_time = end_time - start_time

        # Show results
        print(f"\n✅ All requests completed in {total_time:.2f}s\n")

        for i, result in enumerate(responses):
            if isinstance(result, Exception):
                print(f"   Request {i + 1}: ❌ {result}")
            else:
                print(
                    f"   Request {i + 1}: ✅ {result.status_code} ({result.elapsed.total_seconds():.2f}s)"
                )

        print(f"\n💡 Note: With thread pool, this would take ~{sum([1, 2, 1])}s")
        print(f"   With async I/O, it took {total_time:.2f}s (concurrent!)")


if __name__ == "__main__":
    # Run basic demo
    asyncio.run(main())

    # Run concurrent requests demo
    # asyncio.run(concurrent_requests_demo())
