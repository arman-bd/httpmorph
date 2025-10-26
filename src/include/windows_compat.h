/**
 * windows_compat.h - Windows compatibility header for POSIX types
 *
 * This header is force-included on Windows to provide POSIX types
 * that are missing from MSVC, particularly ssize_t which is used
 * extensively by nghttp2 and other libraries.
 */

#ifndef HTTPMORPH_WINDOWS_COMPAT_H
#define HTTPMORPH_WINDOWS_COMPAT_H

#ifdef _WIN32
    /* Only apply these definitions on Windows */

    /* Prevent Windows crypto headers from conflicting with BoringSSL */
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOCRYPT
        #define NOCRYPT
    #endif

    /* Include BaseTsd.h to get SSIZE_T definition (signed size_t) */
    #ifndef _BASETSD_H_
        #include <BaseTsd.h>
    #endif

    /* Define ssize_t as SSIZE_T for Windows if not already defined */
    /* Use typedef for both C and C++ to avoid macro expansion issues */
    #ifndef _SSIZE_T_DEFINED
        #define _SSIZE_T_DEFINED
        typedef SSIZE_T ssize_t;
    #endif

#endif /* _WIN32 */

#endif /* HTTPMORPH_WINDOWS_COMPAT_H */
