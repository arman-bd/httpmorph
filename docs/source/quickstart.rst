Quick Start
===========

Basic Requests
--------------

Simple GET request:

.. code-block:: python

   import httpmorph

   response = httpmorph.get('https://httpbin.org/get')
   print(response.status_code)
   print(response.text)

POST with JSON data:

.. code-block:: python

   response = httpmorph.post(
       'https://httpbin.org/post',
       json={'name': 'value'}
   )
   print(response.json())

POST with form data:

.. code-block:: python

   response = httpmorph.post(
       'https://httpbin.org/post',
       data={'field': 'value'}
   )

All HTTP methods:

.. code-block:: python

   httpmorph.get(url)
   httpmorph.post(url, data=...)
   httpmorph.put(url, data=...)
   httpmorph.delete(url)
   httpmorph.head(url)
   httpmorph.patch(url, data=...)
   httpmorph.options(url)

Using Client
------------

Create a client for more control:

.. code-block:: python

   client = httpmorph.Client()
   response = client.get('https://example.com')

Enable HTTP/2:

.. code-block:: python

   client = httpmorph.Client(http2=True)
   response = client.get('https://www.google.com')
   print(response.http_version)  # '2.0'

Set default timeout:

.. code-block:: python

   client = httpmorph.Client(timeout=10)
   response = client.get('https://example.com')

Using Sessions
--------------

Sessions persist cookies and headers:

.. code-block:: python

   session = httpmorph.Session()

   # First request sets cookies
   session.get('https://example.com/login')

   # Subsequent requests include cookies
   session.get('https://example.com/protected')

   # Access cookies
   print(session.cookies)

Use as context manager:

.. code-block:: python

   with httpmorph.Session() as session:
       response = session.get('https://example.com')

Browser Fingerprinting
----------------------

Mimic specific browsers:

.. code-block:: python

   # Chrome browser profile
   session = httpmorph.Session(browser='chrome')

   # Firefox browser profile
   session = httpmorph.Session(browser='firefox')

   # Safari browser profile
   session = httpmorph.Session(browser='safari')

   # Edge browser profile
   session = httpmorph.Session(browser='edge')

   # Random browser
   session = httpmorph.Session(browser='random')

Each browser profile includes:

* Browser-specific User-Agent
* Browser-specific TLS cipher suites
* Browser-specific TLS extensions
* Browser-specific HTTP/2 settings
* JA3 fingerprint matching the browser

Request Parameters
------------------

**Headers:**

.. code-block:: python

   headers = {'User-Agent': 'MyApp/1.0'}
   response = httpmorph.get(url, headers=headers)

**Query parameters:**

.. code-block:: python

   params = {'key': 'value', 'foo': 'bar'}
   response = httpmorph.get(url, params=params)
   # Requests: https://example.com?key=value&foo=bar

**Authentication:**

.. code-block:: python

   # Basic authentication
   response = httpmorph.get(
       url,
       auth=('username', 'password')
   )

**Timeout:**

.. code-block:: python

   # 5 second timeout
   response = httpmorph.get(url, timeout=5)

**Proxy:**

.. code-block:: python

   # HTTP proxy
   response = httpmorph.get(
       url,
       proxy='http://proxy.example.com:8080'
   )

   # With authentication
   response = httpmorph.get(
       url,
       proxy='http://proxy.example.com:8080',
       proxy_auth=('user', 'pass')
   )

   # Proxy dict (requests-compatible)
   proxies = {
       'http': 'http://proxy.example.com:8080',
       'https': 'https://proxy.example.com:8080'
   }
   response = httpmorph.get(url, proxies=proxies)

**SSL verification:**

.. code-block:: python

   # Disable SSL verification (not recommended)
   response = httpmorph.get(url, verify=False)

**Redirects:**

.. code-block:: python

   # Disable redirect following
   response = httpmorph.get(url, allow_redirects=False)

   # Limit max redirects
   response = httpmorph.get(url, max_redirects=5)

Response Object
---------------

Access response data:

.. code-block:: python

   response = httpmorph.get('https://httpbin.org/get')

   # Status
   print(response.status_code)  # 200
   print(response.ok)            # True for 200-399
   print(response.reason)        # 'OK'

   # Content
   print(response.body)          # bytes
   print(response.text)          # str (decoded)
   print(response.content)       # bytes (alias)

   # JSON
   data = response.json()

   # Headers
   print(response.headers)
   print(response.headers['Content-Type'])

   # URL
   print(response.url)           # Final URL after redirects

Timing information:

.. code-block:: python

   print(response.total_time_us)      # Total time in microseconds
   print(response.connect_time_us)    # Connection time
   print(response.tls_time_us)        # TLS handshake time
   print(response.first_byte_time_us) # Time to first byte
   print(response.elapsed)            # As timedelta

TLS information:

.. code-block:: python

   print(response.tls_version)     # 'TLSv1.3'
   print(response.tls_cipher)      # Cipher suite name
   print(response.ja3_fingerprint) # JA3 fingerprint

HTTP version:

.. code-block:: python

   print(response.http_version)  # '1.1' or '2.0'

Redirect history:

.. code-block:: python

   print(len(response.history))  # Number of redirects
   for r in response.history:
       print(r.status_code, r.url)

Error Handling
--------------

Handle exceptions:

.. code-block:: python

   import httpmorph

   try:
       response = httpmorph.get('https://example.com', timeout=5)
       response.raise_for_status()  # Raise on 4xx/5xx
   except httpmorph.Timeout:
       print('Request timed out')
   except httpmorph.ConnectionError:
       print('Connection failed')
   except httpmorph.HTTPError as e:
       print(f'HTTP error: {e.response.status_code}')
   except httpmorph.RequestException as e:
       print(f'Request failed: {e}')

Available exceptions:

* ``RequestException`` - Base exception
* ``HTTPError`` - 4xx/5xx status codes (after raise_for_status)
* ``ConnectionError`` - Connection failures
* ``Timeout`` - Request timeout
* ``TooManyRedirects`` - Max redirects exceeded

File Uploads
------------

Upload files:

.. code-block:: python

   # Single file
   files = {'file': open('report.pdf', 'rb')}
   response = httpmorph.post('https://httpbin.org/post', files=files)

   # Multiple files
   files = {
       'file1': open('report.pdf', 'rb'),
       'file2': open('data.csv', 'rb')
   }
   response = httpmorph.post(url, files=files)

   # With filename and content type
   files = {
       'file': ('report.pdf', open('report.pdf', 'rb'), 'application/pdf')
   }
   response = httpmorph.post(url, files=files)

HTTP/2
------

Enable HTTP/2 support:

.. code-block:: python

   # For all requests in a client
   client = httpmorph.Client(http2=True)
   response = client.get('https://www.google.com')

   # For all requests in a session
   session = httpmorph.Session(browser='chrome', http2=True)
   response = session.get('https://www.google.com')

   # Per-request override
   client = httpmorph.Client(http2=False)
   response = client.get('https://www.google.com', http2=True)

Check HTTP version:

.. code-block:: python

   response = client.get('https://www.google.com')
   if response.http_version == '2.0':
       print('Using HTTP/2')

Async Support
-------------

Use AsyncClient for async/await:

.. code-block:: python

   import asyncio
   import httpmorph

   async def fetch():
       async with httpmorph.AsyncClient() as client:
           response = await client.get('https://httpbin.org/get')
           print(response.status_code)
           return response

   asyncio.run(fetch())

Make concurrent requests:

.. code-block:: python

   import asyncio
   import httpmorph

   async def fetch_all(urls):
       async with httpmorph.AsyncClient() as client:
           tasks = [client.get(url) for url in urls]
           responses = await asyncio.gather(*tasks)
           return responses

   urls = [
       'https://httpbin.org/get',
       'https://httpbin.org/headers',
       'https://httpbin.org/user-agent'
   ]

   responses = asyncio.run(fetch_all(urls))
   for response in responses:
       print(response.status_code, response.url)

Next Steps
----------

* See :doc:`api` for complete API reference
* See :doc:`advanced` for advanced features
* Check the examples directory in the repository
