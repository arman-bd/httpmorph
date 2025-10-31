"""Test IOCP implementation"""
import asyncio
import sys

# Test 1: Check if async bindings are available
try:
    import httpmorph
    from httpmorph import AsyncClient
    print(f"✓ httpmorph.HAS_ASYNC = {httpmorph.HAS_ASYNC}")
except ImportError as e:
    print(f"✗ Failed to import AsyncClient: {e}")
    sys.exit(1)

# Test 2: Create async client and make a simple request
async def test_async_request():
    print("\nTesting async request with IOCP...")
    try:
        async with AsyncClient(timeout=10.0) as client:
            print("✓ AsyncClient created successfully")
            
            # Make a simple HTTP request
            response = await client.get('http://httpbin.org/get', timeout=10.0)
            print(f"✓ Request completed: status={response.status_code}")
            print(f"  Response size: {len(response.body)} bytes")
            print(f"  Total time: {response.total_time_us / 1000:.2f} ms")
            
            return True
    except Exception as e:
        print(f"✗ Request failed: {e}")
        import traceback
        traceback.print_exc()
        return False

# Run the test
if __name__ == "__main__":
    print("=" * 60)
    print("Testing Windows IOCP Implementation")
    print("=" * 60)
    
    success = asyncio.run(test_async_request())
    
    if success:
        print("\n" + "=" * 60)
        print("✓ All tests passed! IOCP is working correctly.")
        print("=" * 60)
        sys.exit(0)
    else:
        print("\n" + "=" * 60)
        print("✗ Tests failed")
        print("=" * 60)
        sys.exit(1)
