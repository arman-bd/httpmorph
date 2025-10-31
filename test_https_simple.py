"""Test HTTPS support"""
import asyncio
import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'src'))

from httpmorph import AsyncClient

async def main():
    print("Testing HTTPS...")
    async with AsyncClient() as client:
        try:
            response = await client.get('https://httpbin.org/get', timeout=10.0)
            print(f"SUCCESS: HTTPS status={response.status_code}, size={len(response.body)}")
            return 0
        except Exception as e:
            print(f"ERROR: {e}")
            return 1

if __name__ == '__main__':
    exit_code = asyncio.run(main())
    sys.exit(exit_code)
