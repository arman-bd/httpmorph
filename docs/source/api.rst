API Reference
=============

.. note::
   This is a minimal API reference. Comprehensive API documentation with detailed
   descriptions, examples, and advanced usage will be added in the next update.

Top-Level Functions
-------------------

.. code-block:: python

   import httpmorph

   # Simple HTTP methods
   response = httpmorph.get(url, **kwargs)
   response = httpmorph.post(url, **kwargs)
   response = httpmorph.put(url, **kwargs)
   response = httpmorph.delete(url, **kwargs)
   response = httpmorph.head(url, **kwargs)
   response = httpmorph.options(url, **kwargs)
   response = httpmorph.patch(url, **kwargs)

Client Class
------------

.. code-block:: python

   import httpmorph

   # Create a client
   client = httpmorph.Client(http2=False, timeout=30)

   # Make requests
   response = client.get(url, **kwargs)
   response = client.post(url, **kwargs)
   # ... other HTTP methods

**Constructor Parameters:**

* ``http2`` (bool): Enable HTTP/2 support. Default: ``False``
* ``timeout`` (int): Default timeout in seconds. Default: ``30``

Session Class
-------------

.. code-block:: python

   import httpmorph

   # Create a session
   session = httpmorph.Session(browser='chrome', http2=False)

   # Use as context manager
   with httpmorph.Session(browser='chrome') as session:
       response = session.get(url)

**Constructor Parameters:**

* ``browser`` (str): Browser to mimic. Options: ``'chrome'``, ``'firefox'``, ``'safari'``, ``'edge'``, ``'random'``. Default: ``'chrome'``
* ``http2`` (bool): Enable HTTP/2 support. Default: ``False``

**Attributes:**

* ``cookie_jar``: Cookie storage for the session

Response Class
--------------

All requests return a ``Response`` object:

.. code-block:: python

   response = httpmorph.get('https://example.com')

**Attributes:**

Basic Response Information
~~~~~~~~~~~~~~~~~~~~~~~~~~

* ``status_code`` (int): HTTP status code
* ``body`` (bytes): Response body
* ``headers`` (dict): Response headers
* ``history`` (list): Redirect history

Timing Information
~~~~~~~~~~~~~~~~~~

* ``total_time_us`` (int): Total request time in microseconds
* ``first_byte_time_us`` (int): Time to first byte in microseconds
* ``connect_time_us`` (int): Connection establishment time in microseconds
* ``tls_time_us`` (int): TLS handshake time in microseconds (HTTPS only)

TLS Information
~~~~~~~~~~~~~~~

* ``tls_version`` (str): TLS version (e.g., ``"TLSv1.3"``)
* ``tls_cipher`` (str): Cipher suite name
* ``ja3_fingerprint`` (str): JA3 TLS fingerprint

HTTP Version Information
~~~~~~~~~~~~~~~~~~~~~~~~

* ``http_version`` (str): HTTP protocol version (e.g., ``"1.1"`` or ``"2.0"``)

Common Request Parameters
-------------------------

Most request methods accept these common parameters:

.. code-block:: python

   response = httpmorph.get(
       url,
       headers=None,       # Dict of HTTP headers
       params=None,        # Dict of URL parameters
       json=None,          # Dict to send as JSON body
       data=None,          # String/bytes to send as body
       timeout=30,         # Request timeout in seconds
       http2=None,         # Override HTTP/2 setting
       follow_redirects=True,  # Follow HTTP redirects
   )

Full API Documentation
----------------------

Comprehensive API documentation including:

* Detailed method signatures
* Parameter descriptions and types
* Return value specifications
* Exception handling
* Advanced configuration options
* Internal APIs

...will be available in the next update.

For now, please refer to:

* The source code in ``src/httpmorph/``
* Examples in the ``examples/`` directory
* Type hints in the code
* The project README
