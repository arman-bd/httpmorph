/**
 * boringssl_compat.h - Windows compatibility layer for BoringSSL
 *
 * This header must be included before any BoringSSL headers on Windows
 * to avoid conflicts with Windows Cryptography API (wincrypt.h) macros.
 */

#ifndef BORINGSSL_COMPAT_H
#define BORINGSSL_COMPAT_H

#ifdef _WIN32

/* Windows headers included by Python.h may define these macros
 * which conflict with BoringSSL type names. Undefine them before
 * including BoringSSL headers. */
#ifdef X509_NAME
#undef X509_NAME
#endif

#ifdef X509_CERT_PAIR
#undef X509_CERT_PAIR
#endif

#ifdef X509_EXTENSIONS
#undef X509_EXTENSIONS
#endif

#ifdef PKCS7_SIGNER_INFO
#undef PKCS7_SIGNER_INFO
#endif

#ifdef OCSP_REQUEST
#undef OCSP_REQUEST
#endif

#ifdef OCSP_RESPONSE
#undef OCSP_RESPONSE
#endif

#endif /* _WIN32 */

#endif /* BORINGSSL_COMPAT_H */
