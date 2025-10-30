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
        "/D_WIN32",
        "/D_CRT_SECURE_NO_WARNINGS",
    ]
    # Note: /std:c11 is only available in VS 2019 16.8+ (MSVC 19.28+)
    # For older versions, MSVC uses C89 with C99/C11 extensions by default
    # We rely on C99 features like loop variable declarations which are supported
    EXTRA_LINK_ARGS = ["ws2_32.lib"]  # Winsock library
else:
    # GCC/Clang flags
    EXTRA_COMPILE_ARGS = [
        "-O1",  # Lower optimization for better ASan reports
        "-g",  # Debug symbols
        "-fsanitize=address",  # AddressSanitizer
        "-fno-omit-frame-pointer",  # Better stack traces
        "-Wall",  # All warnings
        "-Wextra",  # Extra warnings
        "-std=c11",  # C11 standard
    ]
    EXTRA_LINK_ARGS = ["-fsanitize=address"]

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


# Get BoringSSL and nghttp2 paths based on platform
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

        # Try pkg-config for BoringSSL/OpenSSL (system fallback on Linux)
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

        # Use system SSL library on Linux if pkg-config didn't work
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
        # Windows - use vendor BoringSSL build, and vcpkg/MSYS2 for nghttp2
        import os

        vendor_dir = Path("vendor").resolve()
        vendor_boringssl = vendor_dir / "boringssl"

        # BoringSSL paths (always use vendor build)
        # Windows builds output to build/Release/ directory
        boringssl_include = str(vendor_boringssl / "include")
        boringssl_lib = str(vendor_boringssl / "build" / "Release")

        # Check if BoringSSL was built successfully
        if not (vendor_boringssl / "build" / "Release" / "ssl.lib").exists():
            print(f"WARNING: BoringSSL not found at {vendor_boringssl}")
            print("Please run: make setup")

        # nghttp2 paths - prefer vendor build, then vcpkg, then MSYS2
        vendor_nghttp2 = vendor_dir / "nghttp2"

        if (vendor_nghttp2 / "lib" / "includes").exists():
            # Vendor build exists
            print(f"Using vendor nghttp2 from: {vendor_nghttp2}")
            nghttp2_include = str(vendor_nghttp2 / "lib" / "includes")
            nghttp2_lib = str(vendor_nghttp2 / "build" / "lib" / "Release")
        elif (vendor_nghttp2 / "include" / "nghttp2").exists():
            # Vendor build with install structure
            print(f"Using vendor nghttp2 from: {vendor_nghttp2}")
            nghttp2_include = str(vendor_nghttp2 / "include")
            nghttp2_lib = str(vendor_nghttp2 / "build" / "lib" / "Release")
        else:
            # Try vcpkg
            vcpkg_root = os.environ.get("VCPKG_ROOT", "C:/vcpkg")
            vcpkg_installed = Path(vcpkg_root) / "installed" / "x64-windows"

            if vcpkg_installed.exists() and (vcpkg_installed / "include" / "nghttp2").exists():
                print(f"Using vcpkg nghttp2 from: {vcpkg_installed}")
                nghttp2_include = str(vcpkg_installed / "include")
                nghttp2_lib = str(vcpkg_installed / "lib")
            elif Path("/mingw64/include/nghttp2").exists():
                print("Using MSYS2 nghttp2 from: /mingw64")
                nghttp2_include = "/mingw64/include"
                nghttp2_lib = "/mingw64/lib"
            else:
                # Fallback to default paths
                print("WARNING: nghttp2 not found. Install via vcpkg or MSYS2")
                nghttp2_include = "C:/Program Files/nghttp2/include"
                nghttp2_lib = "C:/Program Files/nghttp2/lib"

        # zlib paths - prefer vendor, then vcpkg
        vendor_zlib = vendor_dir / "zlib"
        vcpkg_root = os.environ.get("VCPKG_ROOT", "C:/vcpkg")
        vcpkg_installed = Path(vcpkg_root) / "installed" / "x64-windows"

        if (vendor_zlib / "build" / "Release" / "zlibstatic.lib").exists():
            print(f"Using vendor zlib from: {vendor_zlib}")
            # Need both source dir (for zlib.h) and build dir (for zconf.h)
            zlib_include = [str(vendor_zlib), str(vendor_zlib / "build")]
            zlib_lib = str(vendor_zlib / "build" / "Release")
        elif vcpkg_installed.exists() and (vcpkg_installed / "lib" / "zlib.lib").exists():
            print(f"Using vcpkg zlib from: {vcpkg_installed}")
            zlib_include = str(vcpkg_installed / "include")
            zlib_lib = str(vcpkg_installed / "lib")
        else:
            print("WARNING: zlib not found. Install via vcpkg: vcpkg install zlib:x64-windows")
            zlib_include = None
            zlib_lib = None

        return {
            "openssl_include": boringssl_include,
            "openssl_lib": boringssl_lib,
            "nghttp2_include": nghttp2_include,
            "nghttp2_lib": nghttp2_lib,
            "zlib_include": zlib_include,
            "zlib_lib": zlib_lib,
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
print(f"  BoringSSL include: {LIB_PATHS['openssl_include']}")
print(f"  BoringSSL lib: {LIB_PATHS['openssl_lib']}")
print(f"  nghttp2 include: {LIB_PATHS['nghttp2_include']}")
print(f"  nghttp2 lib: {LIB_PATHS['nghttp2_lib']}")
print()

# Platform-specific compile args and libraries for extensions
if IS_WINDOWS:
    # Use /TP to compile as C++ (required for BoringSSL compatibility on Windows)
    # Define WIN32, _WINDOWS, and OPENSSL_WINDOWS for proper BoringSSL compilation
    EXT_COMPILE_ARGS = [
        "/O2",
        "/DHAVE_NGHTTP2",
        "/DNGHTTP2_STATICLIB",  # Static linking for nghttp2
        "/EHsc",
        "/DWIN32",
        "/D_WINDOWS",
        "/DOPENSSL_WINDOWS",
        "/D_WIN32",
        # Force include windows_compat.h to define ssize_t properly for all compilation units
        "/FIwindows_compat.h",
    ]
    # BoringSSL and nghttp2 library names on Windows (without .lib extension)
    # Links to: ssl.lib, crypto.lib, nghttp2.lib, zlib.lib (or zlibstatic.lib if vendor)
    # Detect which zlib we're using
    vendor_dir = Path("vendor").resolve()
    vendor_zlib = vendor_dir / "zlib"
    if (vendor_zlib / "build" / "Release" / "zlibstatic.lib").exists():
        zlib_lib_name = "zlibstatic"
    else:
        zlib_lib_name = "zlib"
    EXT_LIBRARIES = ["ssl", "crypto", "nghttp2", zlib_lib_name]
    EXT_LINK_ARGS = []  # No special linker flags for Windows
else:
    # Production optimized build
    import platform

    EXT_COMPILE_ARGS = [
        "-std=c11",
        "-O3",
        "-march=native" if platform.machine() in ["x86_64", "AMD64"] else "-mcpu=native",
        "-ffast-math",
        "-DHAVE_NGHTTP2",
    ]
    EXT_LINK_ARGS = []

    # Unix library names
    EXT_LIBRARIES = ["ssl", "crypto", "nghttp2", "z"]

# Define C extension modules
# Build library directories list
BORINGSSL_LIB_DIRS = [LIB_PATHS["openssl_lib"]]

# Build include and library directory lists
INCLUDE_DIRS = [
    str(INCLUDE_DIR),
    str(CORE_DIR),
    str(CORE_DIR / "internal"),  # Add internal headers directory for modular architecture
    str(TLS_DIR),
    str(SRC_DIR / "include"),  # Add src/include for windows_compat.h
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

extensions = [
    # Main httpmorph C extension
    Extension(
        "httpmorph._httpmorph",
        sources=[
            str(BINDINGS_DIR / "_httpmorph.pyx"),
            # Modular C source files (refactored from httpmorph.c)
            str(CORE_DIR / "util.c"),
            str(CORE_DIR / "url.c"),
            str(CORE_DIR / "network.c"),
            str(CORE_DIR / "proxy.c"),
            str(CORE_DIR / "tls.c"),
            str(CORE_DIR / "compression.c"),
            str(CORE_DIR / "cookies.c"),
            str(CORE_DIR / "request.c"),
            str(CORE_DIR / "response.c"),
            str(CORE_DIR / "client.c"),
            str(CORE_DIR / "session.c"),
            str(CORE_DIR / "http1.c"),
            str(CORE_DIR / "http2_logic.c"),
            str(CORE_DIR / "http2_session_manager.c"),
            str(CORE_DIR / "core.c"),
            # Supporting modules
            str(CORE_DIR / "connection_pool.c"),
            str(CORE_DIR / "buffer_pool.c"),
            str(CORE_DIR / "request_builder.c"),
            str(CORE_DIR / "string_intern.c"),
            str(CORE_DIR / "io_engine.c"),
            str(CORE_DIR / "async_request.c"),
            str(CORE_DIR / "async_request_manager.c"),
            str(TLS_DIR / "browser_profiles.c"),
        ],
        include_dirs=INCLUDE_DIRS,
        library_dirs=LIBRARY_DIRS,
        libraries=EXT_LIBRARIES,
        extra_compile_args=EXT_COMPILE_ARGS,
        extra_link_args=EXT_LINK_ARGS,
        language="c++" if IS_WINDOWS else "c",  # Use C++ on Windows for BoringSSL compatibility
    ),
    # HTTP/2 client extension
    Extension(
        "httpmorph._http2",
        sources=[
            str(BINDINGS_DIR / "_http2.pyx"),
            str(CORE_DIR / "http2_client.c"),
        ],
        include_dirs=INCLUDE_DIRS,  # Use same include dirs as main extension
        library_dirs=LIBRARY_DIRS,
        libraries=EXT_LIBRARIES,
        extra_compile_args=EXT_COMPILE_ARGS,
        extra_link_args=EXT_LINK_ARGS,
        language="c++" if IS_WINDOWS else "c",  # Use C++ on Windows for BoringSSL compatibility
    ),
    # Async I/O extension (new!)
    Extension(
        "httpmorph._async",
        sources=[
            str(BINDINGS_DIR / "_async.pyx"),
            # Core async I/O modules (already compiled in main extension, but needed here too)
            str(CORE_DIR / "io_engine.c"),
            str(CORE_DIR / "async_request.c"),
            str(CORE_DIR / "async_request_manager.c"),
            # Dependencies needed by async modules
            str(CORE_DIR / "util.c"),
            str(CORE_DIR / "url.c"),
            str(CORE_DIR / "network.c"),
            str(CORE_DIR / "tls.c"),
            str(CORE_DIR / "request.c"),
            str(CORE_DIR / "response.c"),
            str(CORE_DIR / "buffer_pool.c"),
            str(CORE_DIR / "string_intern.c"),
            str(TLS_DIR / "browser_profiles.c"),
        ],
        include_dirs=INCLUDE_DIRS,
        library_dirs=LIBRARY_DIRS,
        libraries=EXT_LIBRARIES,
        extra_compile_args=EXT_COMPILE_ARGS,
        extra_link_args=EXT_LINK_ARGS,
        language="c++" if IS_WINDOWS else "c",
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
