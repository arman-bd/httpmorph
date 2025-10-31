httpmorph Documentation
=======================

**httpmorph** is a high-performance HTTP client library for Python with browser fingerprinting capabilities.

.. note::
   This is an initial documentation release. Comprehensive documentation with detailed guides,
   API references, and examples will be added in the next update.

Overview
--------

httpmorph combines the speed of C with the convenience of Python, providing:

* **High Performance**: 3-4x faster than the `requests` library
* **Browser Fingerprinting**: Mimic Chrome, Firefox, Safari, and Edge browsers
* **HTTP/2 Support**: Built-in HTTP/2 with httpx-compatible API
* **Connection Pooling**: Automatic connection reuse for improved performance
* **TLS Fingerprinting**: JA3 fingerprint generation and management
* **Async Support**: (Coming soon)

Quick Example
-------------

.. code-block:: python

   import httpmorph

   # Simple GET request
   response = httpmorph.get('https://example.com')
   print(response.status_code)
   print(response.body)

   # Use a session with browser fingerprinting
   with httpmorph.Session(browser='chrome') as session:
       response = session.get('https://example.com')
       print(response.tls_version)
       print(response.ja3_fingerprint)

   # HTTP/2 support
   client = httpmorph.Client(http2=True)
   response = client.get('https://httpbingo.org/get')
   print(response.http_version)  # "2.0"

Performance
-----------

Benchmarked against the `requests` library (100-request sample):

* **Local HTTP**: 3.34x faster
* **Remote HTTP**: 1.83x faster
* **Remote HTTPS**: 3.24x faster

Installation
------------

.. code-block:: bash

   pip install httpmorph

Documentation Contents
----------------------

.. toctree::
   :maxdepth: 2
   :caption: Getting Started:

   quickstart

.. toctree::
   :maxdepth: 2
   :caption: Reference:

   api

Indices and tables
==================

* :ref:`genindex`
* :ref:`modindex`
* :ref:`search`
