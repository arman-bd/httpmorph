Installation
============

Basic Installation
------------------

Install from PyPI:

.. code-block:: bash

   pip install httpmorph

This will install pre-built wheels for:

* Windows (x86_64)
* macOS (Intel and Apple Silicon)
* Linux (x86_64, ARM64)

Build Requirements
------------------

If building from source, you'll need:

**macOS:**

.. code-block:: bash

   brew install cmake ninja libnghttp2

**Linux (Ubuntu/Debian):**

.. code-block:: bash

   sudo apt-get install cmake ninja-build libssl-dev pkg-config \
                        autoconf automake libtool libnghttp2-dev

**Linux (Fedora/RHEL):**

.. code-block:: bash

   sudo dnf install cmake ninja-build openssl-devel pkg-config \
                    autoconf automake libtool libnghttp2-devel

**Windows:**

.. code-block:: bash

   choco install cmake golang nasm visualstudio2022buildtools -y

Building from Source
--------------------

.. code-block:: bash

   git clone https://github.com/arman-bd/httpmorph.git
   cd httpmorph

   # Build vendor dependencies (BoringSSL, nghttp2)
   ./scripts/setup_vendors.sh

   # Build Python extensions
   python setup.py build_ext --inplace

   # Install in development mode
   pip install -e ".[dev]"

The first build takes 5-10 minutes to compile BoringSSL. Subsequent builds are faster.

Dependencies
------------

httpmorph has no Python runtime dependencies. All required libraries are built from source:

* **BoringSSL** - TLS implementation (built from source)
* **nghttp2** - HTTP/2 library (system or built from source)
* **zlib** - Compression support (system library)

Optional Dependencies
---------------------

For development:

.. code-block:: bash

   pip install httpmorph[dev]

This includes:

* pytest - Testing framework
* pytest-asyncio - Async test support
* pytest-benchmark - Performance testing
* pytest-cov - Code coverage
* mypy - Type checking
* ruff - Linting and formatting

Troubleshooting
---------------

**Import Error on Linux:**

If you see ``ImportError: cannot open shared object file``, install nghttp2:

.. code-block:: bash

   # Ubuntu/Debian
   sudo apt-get install libnghttp2-14

   # Fedora/RHEL
   sudo dnf install libnghttp2

**Build Errors on macOS:**

Make sure Xcode Command Line Tools are installed:

.. code-block:: bash

   xcode-select --install

**Build Errors on Windows:**

Ensure Visual Studio 2019+ with C++ build tools is installed.

Verifying Installation
----------------------

.. code-block:: python

   import httpmorph

   print(httpmorph.__version__)
   print(httpmorph.version())  # C library version

   # Test basic functionality
   response = httpmorph.get('https://httpbin.org/get')
   assert response.status_code == 200
