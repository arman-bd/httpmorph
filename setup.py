#!/usr/bin/env python3
"""
Build configuration for httpmorph with C extensions and Cython bindings.

This setup.py configures:
1. C extension modules with io_uring/epoll, llhttp, and BoringSSL
2. Cython bindings for Python interface
3. Platform-specific optimizations (io_uring on Linux 5.1+)
"""

import platform
from pathlib import Path

from Cython.Build import cythonize
from setuptools import Extension, setup

# Detect platform
IS_LINUX = platform.system() == "Linux"
IS_WINDOWS = platform.system() == "Windows"
IS_MACOS = platform.system() == "Darwin"
HAS_IO_URING = False

if IS_LINUX:
    try:
        # Check kernel version for io_uring support (Linux 5.1+)
        kernel_version = platform.release().split(".")
        major, minor = int(kernel_version[0]), int(kernel_version[1])
        HAS_IO_URING = (major > 5) or (major == 5 and minor >= 1)
    except (ValueError, IndexError):
        pass

print(f"Building for platform: {platform.system()}")
print(f"io_uring support: {HAS_IO_URING}")

# Base directories
SRC_DIR = Path("src")
CORE_DIR = SRC_DIR / "core"
TLS_DIR = SRC_DIR / "tls"
BINDINGS_DIR = SRC_DIR / "bindings"
INCLUDE_DIR = Path("include")
VENDOR_DIR = Path("vendor")

# Compiler flags (platform-specific)
if IS_WINDOWS:
    # MSVC flags
    EXTRA_COMPILE_ARGS = [
        "/O2",  # Optimization
        "/W3",  # Warning level 3
        "/std:c11",  # C11 standard
        "/D_WIN32",
        "/D_CRT_SECURE_NO_WARNINGS",
    ]
    EXTRA_LINK_ARGS = ["ws2_32.lib"]  # Winsock library
else:
    # GCC/Clang flags
    EXTRA_COMPILE_ARGS = [
        "-O3",  # Maximum optimization
        "-march=native",  # CPU-specific optimizations
        "-ffast-math",  # Fast math operations
        "-Wall",  # All warnings
        "-Wextra",  # Extra warnings
        "-std=c11",  # C11 standard
    ]
    EXTRA_LINK_ARGS = []

# Add io_uring if available
if HAS_IO_URING:
    EXTRA_COMPILE_ARGS.append("-DHAVE_IO_URING")
    EXTRA_LINK_ARGS.append("-luring")

# Platform-specific flags
if IS_LINUX:
    EXTRA_COMPILE_ARGS.extend(["-D_GNU_SOURCE"])

# Include directories
INCLUDE_DIRS = [
    str(INCLUDE_DIR),
    str(CORE_DIR),
    str(TLS_DIR),
    str(VENDOR_DIR / "llhttp" / "include"),
    str(VENDOR_DIR / "boringssl" / "include"),
]

# Library directories
LIBRARY_DIRS = [
    str(VENDOR_DIR / "llhttp" / "build"),
    str(VENDOR_DIR / "boringssl" / "build" / "ssl"),
    str(VENDOR_DIR / "boringssl" / "build" / "crypto"),
]

# Libraries to link
LIBRARIES = ["ssl", "crypto"]  # BoringSSL
if HAS_IO_URING:
    LIBRARIES.append("uring")
if IS_WINDOWS:
    LIBRARIES.extend(["ws2_32", "advapi32", "crypt32", "user32"])

# Check if vendor dependencies exist
VENDOR_EXISTS = (VENDOR_DIR / "boringssl" / "build" / "ssl" / "libssl.a").exists()

if not VENDOR_EXISTS:
    print("\n" + "=" * 70)
    print("WARNING: Vendor dependencies not found!")
    print("=" * 70)
    print("\nPlease run: make setup")
    print("\nThis will download and build:")
    print("  • BoringSSL (TLS library)")
    print("  • liburing (io_uring - Linux only)")
    print("  • nghttp2 (HTTP/2 library)")
    print("\n" + "=" * 70 + "\n")


# Get OpenSSL and nghttp2 paths based on platform
def get_library_paths():
    """Detect platform and return appropriate library paths."""
    import subprocess

    if IS_MACOS:
        # Try Homebrew paths
        try:
            openssl_prefix = (
                subprocess.check_output(
                    ["brew", "--prefix", "openssl@3"], stderr=subprocess.DEVNULL
                )
                .decode()
                .strip()
            )
        except (subprocess.CalledProcessError, FileNotFoundError):
            openssl_prefix = "/opt/homebrew/opt/openssl@3"

        try:
            nghttp2_prefix = (
                subprocess.check_output(
                    ["brew", "--prefix", "libnghttp2"], stderr=subprocess.DEVNULL
                )
                .decode()
                .strip()
            )
        except (subprocess.CalledProcessError, FileNotFoundError):
            nghttp2_prefix = "/opt/homebrew/opt/libnghttp2"

        return {
            "openssl_include": f"{openssl_prefix}/include",
            "openssl_lib": f"{openssl_prefix}/lib",
            "nghttp2_include": f"{nghttp2_prefix}/include",
            "nghttp2_lib": f"{nghttp2_prefix}/lib",
        }

    elif IS_LINUX:
        # Use vendor dependencies if available, otherwise system paths
        vendor_dir = Path("vendor").resolve()

        # Check if vendor nghttp2 exists
        vendor_nghttp2 = vendor_dir / "nghttp2" / "install"
        if vendor_nghttp2.exists() and (vendor_nghttp2 / "include").exists():
            nghttp2_include = str(vendor_nghttp2 / "include")
            nghttp2_lib = str(vendor_nghttp2 / "lib")
            print(f"Using vendor nghttp2 from: {vendor_nghttp2}")
        else:
            # Try pkg-config for nghttp2
            nghttp2_include = None
            nghttp2_lib = None
            try:
                import subprocess

                include_output = (
                    subprocess.check_output(
                        ["pkg-config", "--cflags-only-I", "libnghttp2"], stderr=subprocess.DEVNULL
                    )
                    .decode()
                    .strip()
                )
                if include_output:
                    nghttp2_include = include_output.replace("-I", "").strip()

                lib_output = (
                    subprocess.check_output(
                        ["pkg-config", "--libs-only-L", "libnghttp2"], stderr=subprocess.DEVNULL
                    )
                    .decode()
                    .strip()
                )
                if lib_output:
                    nghttp2_lib = lib_output.replace("-L", "").strip()
            except (subprocess.CalledProcessError, FileNotFoundError):
                pass

            # Fall back to common system paths if pkg-config didn't work
            if not nghttp2_include:
                nghttp2_include = "/usr/include"
            if not nghttp2_lib:
                nghttp2_lib = "/usr/lib/x86_64-linux-gnu"
                # Also check alternative paths
                if not Path(nghttp2_lib).exists():
                    for alt_path in ["/usr/lib64", "/usr/lib"]:
                        if Path(alt_path).exists():
                            nghttp2_lib = alt_path
                            break

        # Try pkg-config for OpenSSL
        openssl_include = None
        openssl_lib = None
        try:
            import subprocess

            include_output = (
                subprocess.check_output(
                    ["pkg-config", "--cflags-only-I", "openssl"], stderr=subprocess.DEVNULL
                )
                .decode()
                .strip()
            )
            if include_output:
                openssl_include = include_output.replace("-I", "").strip()

            lib_output = (
                subprocess.check_output(
                    ["pkg-config", "--libs-only-L", "openssl"], stderr=subprocess.DEVNULL
                )
                .decode()
                .strip()
            )
            if lib_output:
                openssl_lib = lib_output.replace("-L", "").strip()
        except (subprocess.CalledProcessError, FileNotFoundError):
            pass

        # Use system OpenSSL on Linux if pkg-config didn't work
        if not openssl_include:
            openssl_include = "/usr/include"
        if not openssl_lib:
            openssl_lib = "/usr/lib/x86_64-linux-gnu"
            if not Path(openssl_lib).exists():
                for alt_path in ["/usr/lib64", "/usr/lib"]:
                    if Path(alt_path).exists():
                        openssl_lib = alt_path
                        break

        # Validate paths before returning
        if not openssl_include or not openssl_include.strip():
            openssl_include = "/usr/include"
        if not openssl_lib or not openssl_lib.strip():
            openssl_lib = "/usr/lib/x86_64-linux-gnu"
        if not nghttp2_include or not nghttp2_include.strip():
            nghttp2_include = "/usr/include"
        if not nghttp2_lib or not nghttp2_lib.strip():
            nghttp2_lib = "/usr/lib/x86_64-linux-gnu"

        return {
            "openssl_include": openssl_include,
            "openssl_lib": openssl_lib,
            "nghttp2_include": nghttp2_include,
            "nghttp2_lib": nghttp2_lib,
        }

    elif IS_WINDOWS:
        # Windows - use vcpkg if available, otherwise default paths
        import os

        vcpkg_root = os.environ.get("VCPKG_ROOT", "C:/vcpkg")
        vcpkg_installed = Path(vcpkg_root) / "installed" / "x64-windows"

        if vcpkg_installed.exists():
            print(f"Using vcpkg dependencies from: {vcpkg_installed}")
            return {
                "openssl_include": str(vcpkg_installed / "include"),
                "openssl_lib": str(vcpkg_installed / "lib"),
                "nghttp2_include": str(vcpkg_installed / "include"),
                "nghttp2_lib": str(vcpkg_installed / "lib"),
            }
        else:
            # Fall back to default paths
            return {
                "openssl_include": "C:/Program Files/OpenSSL/include",
                "openssl_lib": "C:/Program Files/OpenSSL/lib",
                "nghttp2_include": "C:/Program Files/nghttp2/include",
                "nghttp2_lib": "C:/Program Files/nghttp2/lib",
            }
    else:
        # Other platforms - use default system paths
        return {
            "openssl_include": "/usr/include",
            "openssl_lib": "/usr/lib",
            "nghttp2_include": "/usr/include",
            "nghttp2_lib": "/usr/lib",
        }


LIB_PATHS = get_library_paths()

# Debug output
print("\nLibrary paths detected:")
print(f"  OpenSSL include: {LIB_PATHS['openssl_include']}")
print(f"  OpenSSL lib: {LIB_PATHS['openssl_lib']}")
print(f"  nghttp2 include: {LIB_PATHS['nghttp2_include']}")
print(f"  nghttp2 lib: {LIB_PATHS['nghttp2_lib']}")
print()

# Platform-specific compile args and libraries for extensions
if IS_WINDOWS:
    EXT_COMPILE_ARGS = ["/O2", "/DHAVE_NGHTTP2"]
    # vcpkg library names on Windows (without .lib extension)
    # Links to: libssl.lib, libcrypto.lib, nghttp2.lib, zlib.lib
    EXT_LIBRARIES = ["libssl", "libcrypto", "nghttp2", "zlib"]
else:
    EXT_COMPILE_ARGS = ["-std=c11", "-O2", "-DHAVE_NGHTTP2"]
    # Unix library names
    EXT_LIBRARIES = ["ssl", "crypto", "nghttp2", "z"]

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
        include_dirs=[
            str(INCLUDE_DIR),
            str(CORE_DIR),
            str(TLS_DIR),
            LIB_PATHS["openssl_include"],
            LIB_PATHS["nghttp2_include"],
        ],
        library_dirs=[
            LIB_PATHS["openssl_lib"],
            LIB_PATHS["nghttp2_lib"],
        ],
        libraries=EXT_LIBRARIES,
        extra_compile_args=EXT_COMPILE_ARGS,
        language="c",
    ),
    # HTTP/2 client extension
    Extension(
        "httpmorph._http2",
        sources=[
            str(BINDINGS_DIR / "_http2.pyx"),
            str(CORE_DIR / "http2_client.c"),
        ],
        include_dirs=[
            str(CORE_DIR),
            LIB_PATHS["openssl_include"],
            LIB_PATHS["nghttp2_include"],
        ],
        library_dirs=[
            LIB_PATHS["openssl_lib"],
            LIB_PATHS["nghttp2_lib"],
        ],
        libraries=EXT_LIBRARIES,
        extra_compile_args=EXT_COMPILE_ARGS if IS_WINDOWS else ["-std=c11", "-O2"],
        language="c",
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
