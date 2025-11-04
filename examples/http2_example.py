#!/usr/bin/env python3
"""
HTTP/2 Support Example - httpx-like API

httpmorph now supports HTTP/2 just like httpx!
"""

import httpmorph

# Example 1: Using Client with HTTP/2 enabled
print("Example 1: Client with HTTP/2")
print("-" * 40)

client = httpmorph.Client(http2=True)
response = client.get("https://www.google.com")

print(f"Status: {response.status_code}")
print(f"HTTP Version: {response.http_version}")
print()


# Example 2: Using Session with HTTP/2 enabled
print("Example 2: Session with HTTP/2")
print("-" * 40)

with httpmorph.Session(browser="chrome", http2=True) as session:
    response = session.get("https://www.google.com")
    print(f"Status: {response.status_code}")
    print(f"HTTP Version: {response.http_version}")
print()


# Example 3: Per-request HTTP/2 override
print("Example 3: Per-request override")
print("-" * 40)

# Create client with HTTP/2 disabled by default
client = httpmorph.Client(http2=False)

# But enable it for a specific request
response = client.get("https://www.google.com", http2=True)
print(f"HTTP Version: {response.http_version}")
print()


# Example 4: Comparing with httpx API
print("Example 4: httpx API compatibility")
print("-" * 40)
print("""
# httpx syntax:
import httpx
client = httpx.Client(http2=True)
response = client.get('https://www.google.com')

# httpmorph syntax (identical!):
import httpmorph
client = httpmorph.Client(http2=True)
response = client.get('https://www.google.com')
""")
