#!/usr/bin/env python3
"""
Build configuration for httpmorph with C extensions and Cython bindings.

This setup.py detects the OS and delegates to platform-specific build configurations:
- scripts/windows/setup.py
- scripts/linux/setup.py
- scripts/darwin/setup.py

For OS-specific customization, edit the corresponding file in scripts/<os>/setup.py
"""

import platform
import sys
from pathlib import Path

from Cython.Build import cythonize
from setuptools import Extension, setup

# Base directories
SRC_DIR = Path("src")
CORE_DIR = SRC_DIR / "core"
TLS_DIR = SRC_DIR / "tls"
BINDINGS_DIR = SRC_DIR / "bindings"
INCLUDE_DIR = Path("include")

# Detect platform
IS_LINUX = platform.system() == "Linux"
IS_WINDOWS = platform.system() == "Windows"
IS_MACOS = platform.system() == "Darwin"

print(f"Building for platform: {platform.system()}")

# Import OS-specific configuration
if IS_WINDOWS:
    sys.path.insert(0, str(Path("scripts/windows").resolve()))
    import setup as os_setup
elif IS_LINUX:
    sys.path.insert(0, str(Path("scripts/linux").resolve()))
    import setup as os_setup
elif IS_MACOS:
    sys.path.insert(0, str(Path("scripts/darwin").resolve()))
    import setup as os_setup
else:
    print(f"ERROR: Unsupported platform: {platform.system()}")
    sys.exit(1)

# Get OS-specific configuration
LIB_PATHS = os_setup.get_library_paths()
EXT_COMPILE_ARGS = os_setup.get_compile_args()
EXT_LINK_ARGS = os_setup.get_link_args()
EXT_LIBRARIES = os_setup.get_libraries()
EXTRA_OBJECTS = os_setup.get_extra_objects()
LANGUAGE = os_setup.get_language()

# Debug output
print("\nLibrary paths detected:")
print(f"  BoringSSL include: {LIB_PATHS['openssl_include']}")
print(f"  BoringSSL lib: {LIB_PATHS['openssl_lib']}")
print(f"  nghttp2 include: {LIB_PATHS['nghttp2_include']}")
print(f"  nghttp2 lib: {LIB_PATHS['nghttp2_lib']}")
print()

# Build include and library directory lists
openssl_lib = LIB_PATHS["openssl_lib"]
if isinstance(openssl_lib, list):
    BORINGSSL_LIB_DIRS = openssl_lib
else:
    BORINGSSL_LIB_DIRS = [openssl_lib]

INCLUDE_DIRS = [
    str(INCLUDE_DIR),
    str(CORE_DIR),
    str(TLS_DIR),
    LIB_PATHS["openssl_include"],
    LIB_PATHS["nghttp2_include"],
]

LIBRARY_DIRS = BORINGSSL_LIB_DIRS + [LIB_PATHS["nghttp2_lib"]]

# Add zlib paths on Windows if available
if IS_WINDOWS and LIB_PATHS.get("zlib_include"):
    zlib_inc = LIB_PATHS["zlib_include"]
    if isinstance(zlib_inc, list):
        INCLUDE_DIRS.extend(zlib_inc)
    else:
        INCLUDE_DIRS.append(zlib_inc)
    LIBRARY_DIRS.append(LIB_PATHS["zlib_lib"])

# Define C extension modules
extensions = [
    # Main httpmorph C extension
    Extension(
        "httpmorph._httpmorph",
        sources=[
            str(BINDINGS_DIR / "_httpmorph.pyx"),
            str(CORE_DIR / "httpmorph.c"),
            str(CORE_DIR / "io_engine.c"),
            str(TLS_DIR / "browser_profiles.c"),
        ],
        include_dirs=INCLUDE_DIRS,
        library_dirs=LIBRARY_DIRS,
        libraries=EXT_LIBRARIES,
        extra_objects=EXTRA_OBJECTS,
        extra_link_args=EXT_LINK_ARGS,
        extra_compile_args=EXT_COMPILE_ARGS,
        language=LANGUAGE,
    ),
    # HTTP/2 client extension
    Extension(
        "httpmorph._http2",
        sources=[
            str(BINDINGS_DIR / "_http2.pyx"),
            str(CORE_DIR / "http2_client.c"),
        ],
        include_dirs=[str(CORE_DIR)] + [LIB_PATHS["openssl_include"], LIB_PATHS["nghttp2_include"]],
        library_dirs=LIBRARY_DIRS,
        libraries=EXT_LIBRARIES,
        extra_objects=EXTRA_OBJECTS,
        extra_link_args=EXT_LINK_ARGS,
        extra_compile_args=EXT_COMPILE_ARGS if IS_WINDOWS else ["-std=c11", "-O2"],
        language=LANGUAGE,
    ),
]

# Cythonize extensions
ext_modules = cythonize(
    extensions,
    compiler_directives={
        "language_level": "3",
        "embedsignature": True,
        "boundscheck": False,  # Disable bounds checking for speed
        "wraparound": False,  # Disable negative indexing for speed
        "cdivision": True,  # Use C division semantics
        "initializedcheck": False,  # Disable initialization checks for speed
    },
    annotate=True,  # Generate HTML annotation files
)

if __name__ == "__main__":
    setup(ext_modules=ext_modules)
