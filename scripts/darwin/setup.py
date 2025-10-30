#!/usr/bin/env python3
"""
macOS (Darwin)-specific build configuration for httpmorph.

This module handles macOS-specific:
- Library path detection (BoringSSL, nghttp2)
- Clang compiler flags
- Universal binary support
"""

import os
import subprocess
from pathlib import Path


def get_library_paths():
    """Get macOS-specific library paths for BoringSSL and nghttp2."""
    # Use vendor-built libraries for wheel compatibility
    vendor_dir = Path("vendor").resolve()

    # BoringSSL (always vendor-built)
    vendor_boringssl = vendor_dir / "boringssl"
    boringssl_include = str(vendor_boringssl / "include")

    # Check where BoringSSL actually built the libraries
    # It could be in build/ssl/, build/crypto/, or just build/
    build_dir = vendor_boringssl / "build"

    # Debug: list what's actually in the build directory
    if build_dir.exists():
        print("\n=== BoringSSL build directory contents ===")
        for item in os.listdir(build_dir):
            item_path = build_dir / item
            if item_path.is_dir():
                print(f"  DIR:  {item}/")
                # Check for .a files in subdirectories
                try:
                    for subitem in os.listdir(item_path):
                        if subitem.endswith(".a"):
                            print(f"    LIB: {subitem}")
                except PermissionError:
                    pass
            elif item.endswith(".a"):
                print(f"  LIB:  {item}")
        print("=" * 43 + "\n")

    # Determine library directory based on what exists
    if (build_dir / "ssl" / "libssl.a").exists():
        boringssl_lib = str(build_dir / "ssl")
        print(f"Using BoringSSL from: {boringssl_lib}")
    elif (build_dir / "libssl.a").exists():
        boringssl_lib = str(build_dir)
        print(f"Using BoringSSL from: {boringssl_lib}")
    else:
        # Fallback to build/ssl even if it doesn't exist yet
        boringssl_lib = str(build_dir / "ssl")
        print(f"WARNING: BoringSSL libraries not found, using default: {boringssl_lib}")

    # nghttp2 - prefer vendor build for wheel compatibility
    vendor_nghttp2 = vendor_dir / "nghttp2" / "install"
    if vendor_nghttp2.exists() and (vendor_nghttp2 / "include").exists():
        print(f"Using vendor nghttp2 from: {vendor_nghttp2}")
        nghttp2_include = str(vendor_nghttp2 / "include")
        nghttp2_lib = str(vendor_nghttp2 / "lib")
    else:
        # Fall back to Homebrew if vendor not available
        try:
            nghttp2_prefix = (
                subprocess.check_output(
                    ["brew", "--prefix", "libnghttp2"], stderr=subprocess.DEVNULL
                )
                .decode()
                .strip()
            )
            nghttp2_include = f"{nghttp2_prefix}/include"
            nghttp2_lib = f"{nghttp2_prefix}/lib"
        except (subprocess.CalledProcessError, FileNotFoundError):
            nghttp2_include = "/opt/homebrew/opt/libnghttp2/include"
            nghttp2_lib = "/opt/homebrew/opt/libnghttp2/lib"

    return {
        "openssl_include": boringssl_include,
        "openssl_lib": boringssl_lib,
        "nghttp2_include": nghttp2_include,
        "nghttp2_lib": nghttp2_lib,
    }


def get_compile_args():
    """Get macOS-specific compiler arguments."""
    return [
        "-std=c11",
        "-O2",
        "-DHAVE_NGHTTP2",
    ]


def get_link_args():
    """Get macOS-specific linker arguments."""
    return []


def get_libraries():
    """Get macOS-specific libraries to link against."""
    return ["ssl", "crypto", "nghttp2", "z"]


def get_extra_objects():
    """Get extra object files to link (macOS doesn't need this)."""
    return []


def get_language():
    """Get language for compilation (C for macOS)."""
    return "c"
