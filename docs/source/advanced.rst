Advanced Usage
==============

This page covers advanced features and use cases.

TLS Fingerprinting
------------------

Browser-Specific Fingerprints
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Each browser profile generates a unique TLS fingerprint:

.. code-block:: python

   # Chrome profile
   session = httpmorph.Session(browser='chrome')
   response = session.get('https://example.com')
   print(response.ja3_fingerprint)
   # Output: Chrome-specific JA3 hash

   # Firefox profile
   session = httpmorph.Session(browser='firefox')
   response = session.get('https://example.com')
   print(response.ja3_fingerprint)
   # Output: Firefox-specific JA3 hash

GREASE Values
~~~~~~~~~~~~~

Chrome and Edge profiles use GREASE (Generate Random Extensions And Sustain Extensibility) values that are randomized per request:

.. code-block:: python

   session = httpmorph.Session(browser='chrome')

   # Each request gets different GREASE values
   r1 = session.get('https://example.com')
   r2 = session.get('https://example.com')

   # JA3 fingerprints will differ slightly due to GREASE

TLS Version Control
~~~~~~~~~~~~~~~~~~~

Constrain TLS versions:

.. code-block:: python

   # Force TLS 1.2
   response = httpmorph.get(url, tls_version='1.2')

   # Specify min and max TLS versions
   response = httpmorph.get(
       url,
       tls_version=(0x0303, 0x0304)  # TLS 1.2 to 1.3
   )

Version mapping:

* ``'1.0'`` or ``0x0301`` - TLS 1.0
* ``'1.1'`` or ``0x0302`` - TLS 1.1
* ``'1.2'`` or ``0x0303`` - TLS 1.2
* ``'1.3'`` or ``0x0304`` - TLS 1.3

SSL Certificate Verification
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Load custom CA bundle:

.. code-block:: python

   import certifi
   import httpmorph

   client = httpmorph.Client()
   client.load_ca_file(certifi.where())

   response = client.get('https://example.com')

Disable verification (not recommended):

.. code-block:: python

   response = httpmorph.get(url, verify=False)

Connection Pooling
------------------

Automatic Connection Reuse
~~~~~~~~~~~~~~~~~~~~~~~~~~

Connections are automatically pooled and reused:

.. code-block:: python

   client = httpmorph.Client()

   # First request creates connection
   r1 = client.get('https://example.com/page1')

   # Second request reuses connection
   r2 = client.get('https://example.com/page2')

Sessions also benefit from connection pooling:

.. code-block:: python

   with httpmorph.Session() as session:
       for i in range(100):
           response = session.get(f'https://example.com/page{i}')
           # All requests reuse the same connection

Check timing to verify connection reuse:

.. code-block:: python

   client = httpmorph.Client()

   r1 = client.get('https://example.com')
   print(r1.connect_time_us, r1.tls_time_us)  # Non-zero

   r2 = client.get('https://example.com')
   print(r2.connect_time_us, r2.tls_time_us)  # Much lower or zero

HTTP/2 Details
--------------

ALPN Negotiation
~~~~~~~~~~~~~~~~

HTTP/2 is negotiated via ALPN during TLS handshake:

.. code-block:: python

   client = httpmorph.Client(http2=True)
   response = client.get('https://www.google.com')

   print(response.http_version)  # '2.0' if server supports it

Fallback to HTTP/1.1
~~~~~~~~~~~~~~~~~~~~

If the server doesn't support HTTP/2, httpmorph falls back to HTTP/1.1:

.. code-block:: python

   client = httpmorph.Client(http2=True)
   response = client.get('https://old-server.com')

   print(response.http_version)  # May be '1.1' if server lacks HTTP/2

Per-Request Override
~~~~~~~~~~~~~~~~~~~~

Override HTTP/2 setting per request:

.. code-block:: python

   # Client defaults to HTTP/1.1
   client = httpmorph.Client(http2=False)

   # Force HTTP/2 for this request
   r1 = client.get('https://www.google.com', http2=True)
   print(r1.http_version)  # '2.0'

   # Use default (HTTP/1.1) for this request
   r2 = client.get('https://example.com')
   print(r2.http_version)  # '1.1'

Proxy Configuration
-------------------

HTTP Proxy
~~~~~~~~~~

Route requests through HTTP proxy:

.. code-block:: python

   response = httpmorph.get(
       'http://example.com',
       proxy='http://proxy.example.com:8080'
   )

HTTPS Proxy (CONNECT Tunnel)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

For HTTPS requests, httpmorph uses CONNECT tunneling:

.. code-block:: python

   response = httpmorph.get(
       'https://example.com',
       proxy='http://proxy.example.com:8080'
   )

Proxy Authentication
~~~~~~~~~~~~~~~~~~~~

Basic authentication for proxies:

.. code-block:: python

   response = httpmorph.get(
       url,
       proxy='http://proxy.example.com:8080',
       proxy_auth=('username', 'password')
   )

Or embed credentials in URL:

.. code-block:: python

   response = httpmorph.get(
       url,
       proxy='http://user:pass@proxy.example.com:8080'
   )

Per-Protocol Proxies
~~~~~~~~~~~~~~~~~~~~

Use different proxies for HTTP and HTTPS:

.. code-block:: python

   proxies = {
       'http': 'http://http-proxy.example.com:8080',
       'https': 'http://https-proxy.example.com:8080'
   }

   response = httpmorph.get(url, proxies=proxies)

Async I/O
---------

Non-Blocking Architecture
~~~~~~~~~~~~~~~~~~~~~~~~~

AsyncClient uses epoll (Linux) or kqueue (macOS/BSD) for non-blocking I/O:

.. code-block:: python

   import asyncio
   import httpmorph

   async def main():
       async with httpmorph.AsyncClient() as client:
           # Non-blocking request
           response = await client.get('https://httpbin.org/delay/2')
           print(response.status_code)

   asyncio.run(main())

Concurrent Requests
~~~~~~~~~~~~~~~~~~~

Make multiple requests concurrently:

.. code-block:: python

   import asyncio
   import httpmorph

   async def fetch_all():
       urls = [
           'https://httpbin.org/get',
           'https://httpbin.org/headers',
           'https://httpbin.org/user-agent',
           'https://httpbin.org/ip'
       ]

       async with httpmorph.AsyncClient() as client:
           tasks = [client.get(url) for url in urls]
           responses = await asyncio.gather(*tasks)

           for url, response in zip(urls, responses):
               print(f'{url}: {response.status_code}')

   asyncio.run(fetch_all())

Known Limitations
~~~~~~~~~~~~~~~~~

* DNS resolution is blocking (uses getaddrinfo)
* No automatic retry on connection failures

Performance Optimization
------------------------

Timing Analysis
~~~~~~~~~~~~~~~

Analyze request performance:

.. code-block:: python

   response = httpmorph.get('https://example.com')

   print(f'Connection: {response.connect_time_us / 1000:.2f}ms')
   print(f'TLS:        {response.tls_time_us / 1000:.2f}ms')
   print(f'First byte: {response.first_byte_time_us / 1000:.2f}ms')
   print(f'Total:      {response.total_time_us / 1000:.2f}ms')

Reuse Sessions
~~~~~~~~~~~~~~

Always use sessions for multiple requests to the same host:

.. code-block:: python

   # Bad - creates new connection each time
   for i in range(100):
       httpmorph.get('https://example.com')

   # Good - reuses connection
   session = httpmorph.Session()
   for i in range(100):
       session.get('https://example.com')

Use AsyncClient for I/O-Bound Work
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

For multiple concurrent requests:

.. code-block:: python

   # Synchronous - slow
   urls = ['https://example.com'] * 100
   for url in urls:
       httpmorph.get(url)

   # Asynchronous - fast
   async def fetch_all():
       async with httpmorph.AsyncClient() as client:
           tasks = [client.get(url) for url in urls]
           await asyncio.gather(*tasks)

   asyncio.run(fetch_all())

Response Streaming
------------------

Large File Downloads
~~~~~~~~~~~~~~~~~~~~

Stream large responses to avoid loading entire body into memory:

.. code-block:: python

   response = httpmorph.get('https://example.com/large-file.zip', stream=True)

   with open('large-file.zip', 'wb') as f:
       for chunk in response.iter_content(chunk_size=8192):
           f.write(chunk)

Line-by-Line Processing
~~~~~~~~~~~~~~~~~~~~~~~~

Process responses line by line:

.. code-block:: python

   response = httpmorph.get('https://example.com/large-log.txt', stream=True)

   for line in response.iter_lines():
       process_line(line)

Error Handling Patterns
-----------------------

Retry Logic
~~~~~~~~~~~

Implement retry with exponential backoff:

.. code-block:: python

   import time
   import httpmorph

   def fetch_with_retry(url, max_retries=3):
       for attempt in range(max_retries):
           try:
               response = httpmorph.get(url, timeout=10)
               response.raise_for_status()
               return response
           except (httpmorph.Timeout, httpmorph.ConnectionError):
               if attempt < max_retries - 1:
                   time.sleep(2 ** attempt)  # Exponential backoff
               else:
                   raise

   response = fetch_with_retry('https://example.com')

Graceful Degradation
~~~~~~~~~~~~~~~~~~~~

Handle errors gracefully:

.. code-block:: python

   def safe_fetch(url):
       try:
           response = httpmorph.get(url, timeout=5)
           response.raise_for_status()
           return response.json()
       except httpmorph.Timeout:
           print(f'Timeout fetching {url}')
           return None
       except httpmorph.HTTPError as e:
           print(f'HTTP {e.response.status_code}: {url}')
           return None
       except httpmorph.ConnectionError:
           print(f'Connection failed: {url}')
           return None

Custom Headers
--------------

Persistent Headers in Sessions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Set headers that persist across all requests:

.. code-block:: python

   session = httpmorph.Session()
   session.headers = {
       'User-Agent': 'MyApp/1.0',
       'Accept-Language': 'en-US,en;q=0.9'
   }

   # All requests include these headers
   session.get('https://example.com/page1')
   session.get('https://example.com/page2')

Header Precedence
~~~~~~~~~~~~~~~~~

Per-request headers override session headers:

.. code-block:: python

   session = httpmorph.Session()
   session.headers = {'User-Agent': 'MyApp/1.0'}

   # Uses session User-Agent
   session.get('https://example.com')

   # Overrides session User-Agent for this request
   session.get('https://example.com', headers={'User-Agent': 'CustomBot/2.0'})

Cookie Handling
---------------

Manual Cookie Management
~~~~~~~~~~~~~~~~~~~~~~~~

Manually set and get cookies:

.. code-block:: python

   session = httpmorph.Session()

   # Set cookies manually
   session.cookies = {'session_id': 'abc123'}

   # Make request with cookies
   response = session.get('https://example.com')

   # Get cookies from response
   print(session.cookies)

Accessing Cookie Details
~~~~~~~~~~~~~~~~~~~~~~~~~

Check cookie count:

.. code-block:: python

   session = httpmorph.Session()
   session.get('https://example.com')

   print(len(session.cookie_jar))  # Number of cookies

Note: httpmorph has basic cookie support. It doesn't handle cookie attributes like Domain, Path, or Expires.

Debugging
---------

Print Request Details
~~~~~~~~~~~~~~~~~~~~~

.. code-block:: python

   response = httpmorph.get('https://httpbin.org/get')

   # Request details (stored in response)
   print('URL:', response.url)
   print('Status:', response.status_code)
   print('Headers:', response.headers)

   # Timing breakdown
   print('Connect:', response.connect_time_us / 1000, 'ms')
   print('TLS:', response.tls_time_us / 1000, 'ms')
   print('Total:', response.total_time_us / 1000, 'ms')

TLS Debugging
~~~~~~~~~~~~~

.. code-block:: python

   response = httpmorph.get('https://example.com')

   print('TLS Version:', response.tls_version)
   print('Cipher Suite:', response.tls_cipher)
   print('JA3 Fingerprint:', response.ja3_fingerprint)

Check HTTP Version
~~~~~~~~~~~~~~~~~~

.. code-block:: python

   client = httpmorph.Client(http2=True)
   response = client.get('https://www.google.com')

   if response.http_version == '2.0':
       print('Using HTTP/2')
   else:
       print('Fell back to HTTP/1.1')
