"""Simple async test"""
import asyncio

async def main():
    try:
        import httpmorph
        print(f"HAS_ASYNC = {httpmorph.HAS_ASYNC}")
        
        if not httpmorph.HAS_ASYNC:
            print("Async not available")
            return
        
        from httpmorph import AsyncClient
        
        async with AsyncClient(timeout=5.0) as client:
            print("AsyncClient created")
            response = await client.get('http://httpbin.org/get', timeout=5.0)
            print(f"SUCCESS: status={response.status_code}, size={len(response.body)}")
            
    except Exception as e:
        print(f"ERROR: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    asyncio.run(main())
