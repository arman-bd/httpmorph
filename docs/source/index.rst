httpmorph
=========

A Python HTTP client library with browser fingerprinting capabilities, written in C for performance.

.. code-block:: python

   import httpmorph

   # Simple GET request
   response = httpmorph.get('https://example.com')
   print(response.status_code, response.text)

   # Use a session with browser profile
   session = httpmorph.Session(browser='chrome')
   response = session.get('https://example.com')

Features
--------

* **C implementation** - Native C code with Python bindings
* **Browser profiles** - Mimic Chrome, Firefox, Safari, or Edge
* **HTTP/2 support** - ALPN negotiation via nghttp2
* **TLS fingerprinting** - JA3 fingerprint generation
* **Connection pooling** - Automatic connection reuse
* **Async support** - AsyncClient with epoll/kqueue
* **Compression** - Automatic gzip/deflate decompression

Requirements
------------

* Python 3.8+
* BoringSSL (built from source during installation)
* libnghttp2 (for HTTP/2 support)

Installation
------------

.. code-block:: bash

   pip install httpmorph

See :doc:`installation` for build requirements and troubleshooting.

Quick Example
-------------

.. code-block:: python

   import httpmorph

   # GET request
   response = httpmorph.get('https://httpbin.org/get')
   print(response.json())

   # POST with JSON
   response = httpmorph.post(
       'https://httpbin.org/post',
       json={'key': 'value'}
   )

   # Session with cookies
   session = httpmorph.Session(browser='chrome')
   response = session.get('https://example.com')
   print(session.cookies)

   # HTTP/2
   client = httpmorph.Client(http2=True)
   response = client.get('https://www.google.com')
   print(response.http_version)  # '2.0'

Documentation
-------------

.. toctree::
   :maxdepth: 2

   installation
   quickstart
   api
   advanced

Status
------

httpmorph is under active development. The API may change between minor versions.

License
-------

MIT License
