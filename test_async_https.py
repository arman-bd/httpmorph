"""Test async HTTPS on Windows"""
import asyncio
import sys
import os

# Add src to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'src'))

try:
    from httpmorph import AsyncClient, HAS_ASYNC
    print(f"HAS_ASYNC = {HAS_ASYNC}")
except ImportError as e:
    print(f"Failed to import: {e}")
    sys.exit(1)

async def main():
    async with AsyncClient(verify=False) as client:
        print("AsyncClient created (verify=False)")
        try:
            response = await client.get('https://httpbin.org/get', timeout=10.0)
            print(f"SUCCESS: status={response.status_code}, size={len(response.body)}")
            return 0
        except Exception as e:
            print(f"ERROR: {e}")
            import traceback
            traceback.print_exc()
            return 1

if __name__ == '__main__':
    exit_code = asyncio.run(main())
    sys.exit(exit_code)
