#!/usr/bin/env python3
"""
Linux-specific build configuration for httpmorph.

This module handles Linux-specific:
- Library path detection (BoringSSL, nghttp2)
- GCC compiler flags
- Static library linking with --whole-archive
- io_uring detection
"""

import platform
import subprocess
from pathlib import Path


def check_io_uring_support():
    """Check if the Linux kernel supports io_uring (5.1+)."""
    try:
        kernel_version = platform.release().split(".")
        major, minor = int(kernel_version[0]), int(kernel_version[1])
        return (major > 5) or (major == 5 and minor >= 1)
    except (ValueError, IndexError):
        return False


def get_library_paths():
    """Get Linux-specific library paths for BoringSSL, nghttp2, and liburing."""
    # Always use vendor-built BoringSSL for wheel compatibility
    vendor_dir = Path("vendor").resolve()

    # BoringSSL (always use vendor-built)
    vendor_boringssl = vendor_dir / "boringssl"
    boringssl_include = str(vendor_boringssl / "include")
    # Return full path to build directory for static library files
    boringssl_lib = str(vendor_boringssl / "build")

    print(f"Using vendor BoringSSL from: {vendor_boringssl}")

    # nghttp2 - prefer vendor build for wheel compatibility
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

    # liburing - use vendor build if io_uring is supported
    liburing_include = None
    liburing_lib = None
    if check_io_uring_support():
        vendor_liburing = vendor_dir / "liburing" / "install"
        if vendor_liburing.exists() and (vendor_liburing / "include").exists():
            liburing_include = str(vendor_liburing / "include")
            liburing_lib = str(vendor_liburing / "lib")
            print(f"Using vendor liburing from: {vendor_liburing}")

    return {
        "openssl_include": boringssl_include,
        "openssl_lib": boringssl_lib,
        "nghttp2_include": nghttp2_include,
        "nghttp2_lib": nghttp2_lib,
        "liburing_include": liburing_include,
        "liburing_lib": liburing_lib,
    }


def get_compile_args():
    """Get Linux-specific compiler arguments."""
    has_io_uring = check_io_uring_support()

    compile_args = [
        "-std=c11",
        "-O2",
        "-DHAVE_NGHTTP2",
        "-D_GNU_SOURCE",
    ]

    if has_io_uring:
        compile_args.append("-DHAVE_IO_URING")
        print(f"io_uring support: enabled (kernel {platform.release()})")
    else:
        print(f"io_uring support: disabled (kernel {platform.release()})")

    return compile_args


def get_link_args():
    """Get Linux-specific linker arguments."""
    vendor_boringssl_build = Path("vendor/boringssl/build")

    if (vendor_boringssl_build / "libssl.a").exists():
        # Use -Wl,--whole-archive to force inclusion of all symbols from static libs
        # This is necessary because BoringSSL symbols need to be fully resolved
        link_args = [
            "-Wl,--whole-archive",
            str(vendor_boringssl_build / "libssl.a"),
            str(vendor_boringssl_build / "libcrypto.a"),
            "-Wl,--no-whole-archive",
            "-lpthread",  # BoringSSL requires pthread
            "-lstdc++",  # BoringSSL is C++ so we need the C++ standard library
        ]

        # Statically link liburing if available to avoid dynamic library dependency issues
        has_io_uring = check_io_uring_support()
        if has_io_uring:
            vendor_liburing = Path("vendor/liburing")
            # Try install directory first
            liburing_static = vendor_liburing / "install" / "lib" / "liburing.a"
            if not liburing_static.exists():
                # Fall back to src directory
                liburing_static = vendor_liburing / "src" / "liburing.a"

            if liburing_static.exists():
                link_args.append(str(liburing_static))
                print(f"Static linking liburing from: {liburing_static}")
            else:
                print("WARNING: liburing.a not found, falling back to dynamic linking")
                link_args.append("-luring")

        print("Static linking BoringSSL libraries with --whole-archive")
        return link_args
    else:
        print(f"WARNING: BoringSSL static libraries not found in {vendor_boringssl_build}")
        return []


def get_libraries():
    """Get Linux-specific libraries to link against."""
    vendor_boringssl_build = Path("vendor/boringssl/build")

    # If BoringSSL is statically linked, don't include ssl and crypto in libraries
    if (vendor_boringssl_build / "libssl.a").exists():
        return ["nghttp2", "z"]
    else:
        return ["ssl", "crypto", "nghttp2", "z"]


def get_extra_objects():
    """Get extra object files to link (not used on Linux, handled via link_args)."""
    return []


def get_language():
    """Get language for compilation (C for Linux)."""
    return "c"
