API Reference
=============

This page documents all public APIs in httpmorph.

Module Functions
----------------

httpmorph.get()
~~~~~~~~~~~~~~~

.. code-block:: python

   response = httpmorph.get(url, **kwargs)

Make a GET request.

**Parameters:**

* ``url`` (str) - URL to request
* ``**kwargs`` - See `Request Parameters`_

**Returns:** ``Response`` object

httpmorph.post()
~~~~~~~~~~~~~~~~

.. code-block:: python

   response = httpmorph.post(url, data=None, json=None, **kwargs)

Make a POST request.

**Parameters:**

* ``url`` (str) - URL to request
* ``data`` (bytes/str/dict) - Request body or form data
* ``json`` (dict) - JSON data to send
* ``**kwargs`` - See `Request Parameters`_

**Returns:** ``Response`` object

httpmorph.put()
~~~~~~~~~~~~~~~

.. code-block:: python

   response = httpmorph.put(url, data=None, **kwargs)

Make a PUT request.

httpmorph.delete()
~~~~~~~~~~~~~~~~~~

.. code-block:: python

   response = httpmorph.delete(url, **kwargs)

Make a DELETE request.

httpmorph.head()
~~~~~~~~~~~~~~~~

.. code-block:: python

   response = httpmorph.head(url, **kwargs)

Make a HEAD request.

httpmorph.patch()
~~~~~~~~~~~~~~~~~

.. code-block:: python

   response = httpmorph.patch(url, data=None, **kwargs)

Make a PATCH request.

httpmorph.options()
~~~~~~~~~~~~~~~~~~~

.. code-block:: python

   response = httpmorph.options(url, **kwargs)

Make an OPTIONS request.

Client Class
------------

.. code-block:: python

   client = httpmorph.Client(http2=False)

HTTP client for making requests.

**Constructor Parameters:**

* ``http2`` (bool) - Enable HTTP/2. Default: ``False``

**Methods:**

All HTTP method functions (get, post, put, delete, head, patch, options) are available:

.. code-block:: python

   response = client.get(url, **kwargs)
   response = client.post(url, data=None, json=None, **kwargs)
   # etc.

**Other Methods:**

.. code-block:: python

   client.load_ca_file(ca_file)

Load CA certificates from file (PEM format).

**Parameters:**

* ``ca_file`` (str) - Path to CA certificate bundle

**Returns:** ``bool`` - True on success

Session Class
-------------

.. code-block:: python

   session = httpmorph.Session(browser='chrome', os='macos', http2=False)

HTTP session with persistent cookies and headers.

**Constructor Parameters:**

* ``browser`` (str) - Browser profile to mimic. Options: ``'chrome'``, ``'chrome142'``, ``'random'``. Default: ``'chrome'``
* ``os`` (str) - Operating system for User-Agent. Options: ``'macos'``, ``'windows'``, ``'linux'``. Default: ``'macos'``
* ``http2`` (bool) - Enable HTTP/2. Default: ``False``

**Attributes:**

* ``cookies`` (dict) - Session cookies
* ``cookie_jar`` - Cookie jar object with length
* ``headers`` (dict) - Persistent headers (can be set)

**Methods:**

All HTTP method functions are available:

.. code-block:: python

   response = session.get(url, **kwargs)
   response = session.post(url, data=None, json=None, **kwargs)
   # etc.

**Context Manager:**

.. code-block:: python

   with httpmorph.Session() as session:
       response = session.get(url)

**Other Methods:**

.. code-block:: python

   session.close()

Explicitly close the session and release resources.

AsyncClient Class
-----------------

.. code-block:: python

   client = httpmorph.AsyncClient()

Async HTTP client using epoll/kqueue.

**Methods:**

All HTTP methods return coroutines:

.. code-block:: python

   response = await client.get(url, **kwargs)
   response = await client.post(url, data=None, json=None, **kwargs)
   # etc.

**Context Manager:**

.. code-block:: python

   async with httpmorph.AsyncClient() as client:
       response = await client.get(url)

**Note:** AsyncClient uses non-blocking I/O but DNS resolution is still blocking.

Response Class
--------------

Returned by all request methods.

Status Attributes
~~~~~~~~~~~~~~~~~

* ``status_code`` (int) - HTTP status code (e.g., 200, 404)
* ``ok`` (bool) - True if status is 200-399
* ``reason`` (str) - Status reason phrase (e.g., "OK", "Not Found")
* ``url`` (str) - Final URL after redirects

Content Attributes
~~~~~~~~~~~~~~~~~~

* ``body`` (bytes) - Raw response body
* ``content`` (bytes) - Alias for body
* ``text`` (str) - Decoded response body (lazy, UTF-8 with fallback)
* ``headers`` (dict) - Response headers

Methods
~~~~~~~

.. code-block:: python

   response.json()

Parse response body as JSON.

**Returns:** Parsed JSON object (dict/list)

**Raises:** ``ValueError`` if body is not valid JSON

.. code-block:: python

   response.raise_for_status()

Raise ``HTTPError`` if status is 4xx or 5xx.

**Raises:** ``HTTPError``

.. code-block:: python

   response.iter_content(chunk_size=1024, decode_unicode=False)

Iterate over response body in chunks.

**Parameters:**

* ``chunk_size`` (int) - Size of chunks in bytes
* ``decode_unicode`` (bool) - Decode chunks as text

**Yields:** bytes or str

.. code-block:: python

   response.iter_lines(delimiter=None, decode_unicode=True)

Iterate over response body line by line.

**Parameters:**

* ``delimiter`` (str) - Line delimiter (default: ``\\n``)
* ``decode_unicode`` (bool) - Decode lines as text

**Yields:** str or bytes

Timing Attributes
~~~~~~~~~~~~~~~~~

All timing values are in microseconds:

* ``total_time_us`` (int) - Total request time
* ``connect_time_us`` (int) - Connection establishment time
* ``tls_time_us`` (int) - TLS handshake time
* ``first_byte_time_us`` (int) - Time to first byte
* ``elapsed`` (timedelta) - Total time as timedelta object

TLS Attributes
~~~~~~~~~~~~~~

For HTTPS requests:

* ``tls_version`` (str) - TLS version used (e.g., "TLSv1.3")
* ``tls_cipher`` (str) - Cipher suite name
* ``ja3_fingerprint`` (str) - JA3 TLS fingerprint

HTTP Version
~~~~~~~~~~~~

* ``http_version`` (str) - HTTP protocol version: "1.0", "1.1", or "2.0"

Redirect Handling
~~~~~~~~~~~~~~~~~

* ``history`` (list) - List of ``Response`` objects from redirects
* ``is_redirect`` (bool) - True if status is 3xx

Streaming
~~~~~~~~~

* ``raw`` (BytesIO) - Raw response stream (lazy)

Error Attributes
~~~~~~~~~~~~~~~~

* ``error`` (int) - Error code (0 if no error)
* ``error_message`` (str) - Human-readable error message

Request Parameters
------------------

Common parameters accepted by all request methods:

HTTP Parameters
~~~~~~~~~~~~~~~

* ``headers`` (dict) - HTTP headers
* ``params`` (dict) - URL query parameters
* ``cookies`` (dict) - Cookies to send
* ``auth`` (tuple) - Basic authentication: ``(username, password)``

Body Parameters
~~~~~~~~~~~~~~~

* ``data`` (bytes/str/dict) - Request body or form data
* ``json`` (dict) - JSON data (auto-serialized with Content-Type header)
* ``files`` (dict) - Files to upload (multipart/form-data)

Connection Parameters
~~~~~~~~~~~~~~~~~~~~~

* ``timeout`` (int/float) - Request timeout in seconds
* ``proxy`` (str) - Proxy URL
* ``proxy_auth`` (tuple) - Proxy authentication: ``(username, password)``
* ``proxies`` (dict) - Proxy dict: ``{'http': '...', 'https': '...'}``

SSL/TLS Parameters
~~~~~~~~~~~~~~~~~~

* ``verify`` or ``verify_ssl`` (bool) - Verify SSL certificates. Default: ``True``
* ``tls_version`` (str/tuple) - TLS version constraint (e.g., ``"1.2"`` or ``(min, max)``)

HTTP/2 Parameters
~~~~~~~~~~~~~~~~~

* ``http2`` (bool) - Enable HTTP/2 for this request (overrides client/session default)

Redirect Parameters
~~~~~~~~~~~~~~~~~~~

* ``allow_redirects`` (bool) - Follow redirects. Default: ``True``
* ``max_redirects`` (int) - Maximum number of redirects. Default: ``10``

Other Parameters
~~~~~~~~~~~~~~~~

* ``stream`` (bool) - Stream response (use with ``iter_content()``)

Exceptions
----------

All exceptions inherit from ``RequestException``.

RequestException
~~~~~~~~~~~~~~~~

Base exception for all httpmorph errors.

HTTPError
~~~~~~~~~

Raised when ``raise_for_status()`` is called on a 4xx/5xx response.

**Attributes:**

* ``response`` - The Response object

ConnectionError
~~~~~~~~~~~~~~~

Raised when connection fails.

Timeout
~~~~~~~

Raised when request times out.

TooManyRedirects
~~~~~~~~~~~~~~~~

Raised when max redirects is exceeded.

Version Information
-------------------

.. code-block:: python

   import httpmorph

   # Python package version
   print(httpmorph.__version__)

   # C library version
   print(httpmorph.version())

Library Initialization
----------------------

.. code-block:: python

   httpmorph.init()

Initialize the library (called automatically on import).

.. code-block:: python

   httpmorph.cleanup()

Cleanup library resources.

Browser Profiles
----------------

Available browser profiles for ``Session(browser=...)``:

Chrome 142
~~~~~~~~~~

The default and most accurate browser profile, mimicking Chrome 142:

**Fingerprint Characteristics:**

* **JA3N**: ``8e19337e7524d2573be54efb2b0784c9`` (perfect match)
* **JA4**: ``t13d1516h2_8daaf6152771_d8a2da3f94cd`` (perfect match)
* **JA4_R**: ``t13d1516h2_002f,0035,009c,...`` (perfect match)
* **TLS 1.3** with 15 cipher suites
* **Post-quantum cryptography**: X25519MLKEM768 (curve 4588)
* **Certificate compression**: Brotli, Zlib
* **GREASE**: Randomized per request
* **HTTP/2**: Chrome-specific SETTINGS frame

**User-Agent Variants:**

* **macOS**: ``Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/142.0.0.0 Safari/537.36``
* **Windows**: ``Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/142.0.0.0 Safari/537.36``
* **Linux**: ``Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/142.0.0.0 Safari/537.36``

**Usage:**

.. code-block:: python

   # Use Chrome 142 profile (default)
   session = httpmorph.Session(browser='chrome')

   # Explicitly use Chrome 142
   session = httpmorph.Session(browser='chrome142')

   # With specific OS
   session = httpmorph.Session(browser='chrome', os='windows')

Random
~~~~~~

Randomly selects a browser profile for each session. Currently only Chrome 142 is available.
