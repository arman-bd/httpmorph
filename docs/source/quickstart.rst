Quick Start Guide
=================

.. note::
   This is a basic quick start guide. More detailed tutorials and examples will be added in the next update.

Installation
------------

Install httpmorph using pip:

.. code-block:: bash

   pip install httpmorph

Basic Usage
-----------

Making Simple Requests
~~~~~~~~~~~~~~~~~~~~~~~

The simplest way to make a request is using the top-level functions:

.. code-block:: python

   import httpmorph

   # GET request
   response = httpmorph.get('https://example.com')
   print(response.status_code)
   print(response.body)

   # POST request with JSON
   data = {'key': 'value'}
   response = httpmorph.post('https://httpbingo.org/post', json=data)

Using the Client
~~~~~~~~~~~~~~~~

For more control, use the ``Client`` class:

.. code-block:: python

   import httpmorph

   # Create a client
   client = httpmorph.Client()

   # Make requests
   response = client.get('https://example.com')

   # Enable HTTP/2
   client = httpmorph.Client(http2=True)
   response = client.get('https://httpbingo.org/get')
   print(response.http_version)  # "2.0"

Using Sessions with Browser Fingerprinting
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Sessions maintain cookies and can mimic specific browsers:

.. code-block:: python

   import httpmorph

   # Create a session mimicking Chrome
   with httpmorph.Session(browser='chrome') as session:
       response = session.get('https://example.com')
       print(response.tls_version)
       print(response.ja3_fingerprint)

   # Available browsers: chrome, firefox, safari, edge, random

HTTP/2 Support
--------------

httpmorph supports HTTP/2 with an httpx-compatible API:

.. code-block:: python

   import httpmorph

   # Enable HTTP/2 for all requests
   client = httpmorph.Client(http2=True)
   response = client.get('https://httpbingo.org/get')

   # Enable HTTP/2 per-request
   client = httpmorph.Client(http2=False)
   response = client.get('https://httpbingo.org/get', http2=True)

Response Objects
----------------

All requests return a ``Response`` object with the following attributes:

.. code-block:: python

   response = httpmorph.get('https://example.com')

   # Basic attributes
   response.status_code      # HTTP status code (e.g., 200)
   response.body             # Response body as bytes
   response.headers          # Response headers as dict

   # Timing information
   response.total_time_us    # Total request time in microseconds
   response.connect_time_us  # Connection time in microseconds
   response.tls_time_us      # TLS handshake time in microseconds

   # TLS information (for HTTPS)
   response.tls_version      # TLS version (e.g., "TLSv1.3")
   response.tls_cipher       # Cipher suite used
   response.ja3_fingerprint  # JA3 fingerprint

   # HTTP/2 information
   response.http_version     # HTTP version (e.g., "2.0")

Next Steps
----------

* Explore the :doc:`api` reference for detailed information
* Check out the examples in the ``examples/`` directory
* Read the full documentation (coming in the next update)

More comprehensive documentation including:

* Advanced usage patterns
* Custom headers and authentication
* Proxy configuration
* Error handling
* Performance tuning
* Browser fingerprinting details
* HTTP/2 configuration

...will be available in the next update.
