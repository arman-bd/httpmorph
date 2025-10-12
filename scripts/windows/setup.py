#!/usr/bin/env python3
"""
Windows-specific build configuration for httpmorph.

This module handles Windows-specific:
- Library path detection (BoringSSL, nghttp2, zlib)
- MSVC compiler flags
- Static library linking
"""

import os
from pathlib import Path


def get_library_paths():
    """Get Windows-specific library paths for BoringSSL, nghttp2, and zlib."""
    vendor_dir = Path("vendor").resolve()
    vendor_boringssl = vendor_dir / "boringssl"

    # BoringSSL paths (always use vendor build)
    # Windows CMake with Visual Studio generator builds to build/ssl/Release/ and build/crypto/Release/
    boringssl_include = str(vendor_boringssl / "include")

    # Check for different possible build locations
    if (vendor_boringssl / "build" / "ssl" / "Release" / "ssl.lib").exists():
        # Visual Studio multi-config generator: build/ssl/Release/
        boringssl_lib = [
            str(vendor_boringssl / "build" / "ssl" / "Release"),
            str(vendor_boringssl / "build" / "crypto" / "Release"),
        ]
        print(f"Using BoringSSL from: {vendor_boringssl / 'build'} (Visual Studio layout)")
    elif (vendor_boringssl / "build" / "Release" / "ssl.lib").exists():
        # Single-config generator with Release: build/Release/
        boringssl_lib = str(vendor_boringssl / "build" / "Release")
        print(f"Using BoringSSL from: {boringssl_lib}")
    elif (vendor_boringssl / "build" / "ssl.lib").exists():
        # Single-config generator: build/
        boringssl_lib = str(vendor_boringssl / "build")
        print(f"Using BoringSSL from: {boringssl_lib}")
    else:
        # Fallback - assume Visual Studio layout
        boringssl_lib = [
            str(vendor_boringssl / "build" / "ssl" / "Release"),
            str(vendor_boringssl / "build" / "crypto" / "Release"),
        ]
        print(f"WARNING: BoringSSL not found, using default: {vendor_boringssl / 'build'}")
        print("Please run: bash scripts/windows/setup_vendors.sh")

    # nghttp2 paths - prefer vendor build, then vcpkg, then MSYS2
    vendor_nghttp2 = vendor_dir / "nghttp2"

    # Check vendor build locations in order of preference
    if (vendor_nghttp2 / "build" / "lib" / "Release" / "nghttp2.lib").exists():
        # CMake build with Visual Studio generator
        print(f"Using vendor nghttp2 from: {vendor_nghttp2}")
        nghttp2_include = str(vendor_nghttp2 / "lib" / "includes")
        nghttp2_lib = str(vendor_nghttp2 / "build" / "lib" / "Release")
    elif (vendor_nghttp2 / "build" / "Release" / "nghttp2.lib").exists():
        # CMake build with single-config generator
        print(f"Using vendor nghttp2 from: {vendor_nghttp2}")
        nghttp2_include = str(vendor_nghttp2 / "lib" / "includes")
        nghttp2_lib = str(vendor_nghttp2 / "build" / "Release")
    elif (vendor_nghttp2 / "lib" / "includes").exists():
        # Vendor build exists (old structure)
        print(f"Using vendor nghttp2 from: {vendor_nghttp2}")
        nghttp2_include = str(vendor_nghttp2 / "lib" / "includes")
        nghttp2_lib = str(vendor_nghttp2 / "build" / "lib" / "Release")
    else:
        # Try vcpkg as fallback
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
            # Fallback to default paths - will fail but show clear error
            print("WARNING: nghttp2 not found. Run: bash scripts/windows/setup_vendors.sh")
            nghttp2_include = str(vendor_nghttp2 / "lib" / "includes")
            nghttp2_lib = str(vendor_nghttp2 / "build" / "lib" / "Release")

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


def get_compile_args():
    """Get Windows-specific compiler arguments."""
    # Use /TP to compile as C++ (required for BoringSSL compatibility on Windows)
    # Define WIN32, _WINDOWS, and OPENSSL_WINDOWS for proper BoringSSL compilation
    # Define NGHTTP2_STATICLIB to link against static nghttp2 library
    return [
        "/TP",
        "/O2",
        "/DHAVE_NGHTTP2",
        "/DNGHTTP2_STATICLIB",
        "/EHsc",
        "/DWIN32",
        "/D_WINDOWS",
        "/DOPENSSL_WINDOWS",
        "/D_WIN32",
    ]


def get_link_args():
    """Get Windows-specific linker arguments."""
    return []


def get_libraries():
    """Get Windows-specific libraries to link against."""
    # Detect which zlib we're using
    vendor_dir = Path("vendor").resolve()
    vendor_zlib = vendor_dir / "zlib"
    if (vendor_zlib / "build" / "Release" / "zlibstatic.lib").exists():
        zlib_lib_name = "zlibstatic"
    else:
        zlib_lib_name = "zlib"

    return ["ssl", "crypto", "nghttp2", zlib_lib_name]


def get_extra_objects():
    """Get extra object files to link (Windows doesn't need this)."""
    return []


def get_language():
    """Get language for compilation (C++ for Windows)."""
    return "c++"
