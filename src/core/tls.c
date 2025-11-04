/**
 * tls.c - TLS/SSL operations and fingerprinting
 */

#include "internal/tls.h"
#include "internal/util.h"

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#pragma comment(lib, "crypt32.lib")
#endif

/* OpenSSL 1.0.x compatibility */
#if OPENSSL_VERSION_NUMBER < 0x10100000L
#define EVP_MD_CTX_new EVP_MD_CTX_create
#define EVP_MD_CTX_free EVP_MD_CTX_destroy
#endif

/**
 * Configure SSL context with browser profile
 */
int httpmorph_configure_ssl_ctx(SSL_CTX *ctx, const browser_profile_t *profile) {
    if (!ctx || !profile) {
        return -1;
    }

    /* Set TLS version range */
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
#ifdef TLS1_3_VERSION
    SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);
#else
    SSL_CTX_set_max_proto_version(ctx, TLS1_2_VERSION);
#endif

    /* Build cipher list string from profile */
    char cipher_list[2048] = {0};
    char *p = cipher_list;

    for (int i = 0; i < profile->cipher_suite_count; i++) {
        uint16_t cs = profile->cipher_suites[i];

        /* Map cipher suite code to BoringSSL name */
        const char *name = NULL;
        switch (cs) {
            case 0x1301: name = "TLS_AES_128_GCM_SHA256"; break;
            case 0x1302: name = "TLS_AES_256_GCM_SHA384"; break;
            case 0x1303: name = "TLS_CHACHA20_POLY1305_SHA256"; break;
            case 0xc02b: name = "ECDHE-ECDSA-AES128-GCM-SHA256"; break;
            case 0xc02f: name = "ECDHE-RSA-AES128-GCM-SHA256"; break;
            case 0xc02c: name = "ECDHE-ECDSA-AES256-GCM-SHA384"; break;
            case 0xc030: name = "ECDHE-RSA-AES256-GCM-SHA384"; break;
            case 0xcca9: name = "ECDHE-ECDSA-CHACHA20-POLY1305"; break;
            case 0xcca8: name = "ECDHE-RSA-CHACHA20-POLY1305"; break;
            default: continue;  /* Skip unsupported */
        }

        if (name) {
            if (p != cipher_list) {
                *p++ = ':';
            }
            strcpy(p, name);
            p += strlen(name);
        }
    }

    /* Set cipher list */
    if (SSL_CTX_set_cipher_list(ctx, cipher_list) != 1) {
        return -1;
    }

    /* Set supported curves */
    if (profile->curve_count > 0) {
        int nids[MAX_CURVES];
        int nid_count = 0;

        for (int i = 0; i < profile->curve_count && nid_count < MAX_CURVES; i++) {
            int nid = -1;
            switch (profile->curves[i]) {
                case 0x001d: nid = NID_X25519; break;
                case 0x0017: nid = NID_X9_62_prime256v1; break;  /* secp256r1 */
                case 0x0018: nid = NID_secp384r1; break;
                case 0x0019: nid = NID_secp521r1; break;
                default: continue;
            }

            if (nid != -1) {
                nids[nid_count++] = nid;
            }
        }

        if (nid_count > 0) {
            /* BoringSSL uses SSL_CTX_set1_groups (curves are now called groups) */
            SSL_CTX_set1_groups(ctx, nids, nid_count);
        }
    }

    /* Set ALPN protocols for HTTP/2 support */
    if (profile->alpn_protocol_count > 0) {
        /* Build ALPN protocol list */
        unsigned char alpn_list[256];
        unsigned char *alpn_p = alpn_list;

        for (int i = 0; i < profile->alpn_protocol_count; i++) {
            size_t len = strlen(profile->alpn_protocols[i]);
            if (alpn_p - alpn_list + len + 1 > sizeof(alpn_list)) {
                break;
            }
            *alpn_p++ = (unsigned char)len;
            memcpy(alpn_p, profile->alpn_protocols[i], len);
            alpn_p += len;
        }

        SSL_CTX_set_alpn_protos(ctx, alpn_list, alpn_p - alpn_list);
    }

    return 0;
}

/**
 * Establish TLS connection on existing socket
 */
SSL* httpmorph_tls_connect(SSL_CTX *ctx, int sockfd, const char *hostname,
                            const browser_profile_t *browser_profile,
                            bool http2_enabled, bool verify_cert, uint64_t *tls_time_us) {
    uint64_t start_time = httpmorph_get_time_us();

    SSL *ssl = SSL_new(ctx);
    if (!ssl) {
        return NULL;
    }

    /* Set SSL verification mode */
    if (verify_cert) {
        SSL_set_verify(ssl, SSL_VERIFY_PEER, NULL);
    } else {
        SSL_set_verify(ssl, SSL_VERIFY_NONE, NULL);
    }

    /* Set ALPN protocols based on http2_enabled flag */
    if (browser_profile && browser_profile->alpn_protocol_count > 0) {
        unsigned char alpn_list[256];
        unsigned char *alpn_p = alpn_list;

        for (int i = 0; i < browser_profile->alpn_protocol_count; i++) {
            /* Skip "h2" if HTTP/2 not enabled */
            if (!http2_enabled && strcmp(browser_profile->alpn_protocols[i], "h2") == 0) {
                continue;
            }

            size_t len = strlen(browser_profile->alpn_protocols[i]);
            *alpn_p++ = (unsigned char)len;
            memcpy(alpn_p, browser_profile->alpn_protocols[i], len);
            alpn_p += len;
        }

        /* Only set ALPN if we have protocols */
        if (alpn_p > alpn_list) {
            SSL_set_alpn_protos(ssl, alpn_list, alpn_p - alpn_list);
        }
    }

    /* Set SNI hostname */
    SSL_set_tlsext_host_name(ssl, hostname);

    /* Attach to socket */
    if (SSL_set_fd(ssl, sockfd) != 1) {
        SSL_free(ssl);
        return NULL;
    }

    /* Perform TLS handshake (handle non-blocking socket) */
    int ret;
    int ssl_err;
    uint64_t handshake_timeout_us = 30000000;  /* 30 seconds */
    uint64_t deadline = start_time + handshake_timeout_us;

    while (1) {
        ret = SSL_connect(ssl);
        if (ret == 1) {
            /* Handshake successful */
            break;
        }

        ssl_err = SSL_get_error(ssl, ret);

        /* Check if we need to wait for I/O */
        if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
            /* Check timeout */
            uint64_t now = httpmorph_get_time_us();
            if (now >= deadline) {
                SSL_free(ssl);
                return NULL;  /* Timeout */
            }

            /* Wait for socket to be ready */
            fd_set read_fds, write_fds;
            struct timeval tv;
            uint64_t remaining_us = deadline - now;

            FD_ZERO(&read_fds);
            FD_ZERO(&write_fds);

            if (ssl_err == SSL_ERROR_WANT_READ) {
                FD_SET(sockfd, &read_fds);
            } else {
                FD_SET(sockfd, &write_fds);
            }

            tv.tv_sec = remaining_us / 1000000;
            tv.tv_usec = remaining_us % 1000000;

            int select_ret = select(SELECT_NFDS(sockfd), &read_fds, &write_fds, NULL, &tv);
            if (select_ret <= 0) {
                SSL_free(ssl);
                return NULL;  /* Timeout or error */
            }

            /* Retry SSL_connect */
            continue;
        }

        /* Other error - handshake failed */
        SSL_free(ssl);
        return NULL;
    }

    *tls_time_us = httpmorph_get_time_us() - start_time;
    return ssl;
}

/**
 * Calculate JA3 fingerprint from SSL connection
 */
char* httpmorph_calculate_ja3(SSL *ssl, const browser_profile_t *profile) {
    if (!ssl) {
        return NULL;
    }

    char ja3_string[4096];
    char *p = ja3_string;
    char *end = ja3_string + sizeof(ja3_string);

    /* Ensure we don't overflow */
    if (end <= p) {
        return NULL;
    }

    /* JA3 Format: TLSVersion,Ciphers,Extensions,EllipticCurves,EllipticCurvePointFormats */

    /* 1. TLS Version */
    int tls_version = SSL_version(ssl);
    uint16_t ja3_version = 0;
    switch (tls_version) {
        case TLS1_VERSION:   ja3_version = 0x0301; break;  /* TLS 1.0 */
        case TLS1_1_VERSION: ja3_version = 0x0302; break;  /* TLS 1.1 */
        case TLS1_2_VERSION: ja3_version = 0x0303; break;  /* TLS 1.2 */
#ifdef TLS1_3_VERSION
        case TLS1_3_VERSION: ja3_version = 0x0304; break;  /* TLS 1.3 */
#endif
        default:             ja3_version = 0x0303; break;  /* Default to TLS 1.2 */
    }

    int written = snprintf(p, SNPRINTF_SIZE(end - p), "%u", ja3_version);
    if (written < 0 || written >= (end - p)) {
        return NULL;
    }
    p += written;

    /* 2. Cipher Suites - use browser profile's cipher list to make it unique */
    if (p < end) *p++ = ',';
    if (profile && profile->cipher_suite_count > 0) {
        for (int i = 0; i < profile->cipher_suite_count && p < end; i++) {
            if (i > 0 && p < end) *p++ = '-';
            written = snprintf(p, SNPRINTF_SIZE(end - p), "%u", profile->cipher_suites[i]);
            if (written > 0 && written < (end - p)) p += written;
        }
    } else {
        /* Fallback: use negotiated cipher */
        const SSL_CIPHER *cipher = SSL_get_current_cipher(ssl);
        if (cipher) {
            uint16_t cipher_id = SSL_CIPHER_get_id(cipher) & 0xFFFF;
            written = snprintf(p, SNPRINTF_SIZE(end - p), "%u", cipher_id);
            if (written > 0 && written < (end - p)) p += written;
        }
    }

    /* 3. Extensions - use browser profile's extension list */
    if (p < end) *p++ = ',';
    if (profile && profile->extension_count > 0) {
        for (int i = 0; i < profile->extension_count && p < end; i++) {
            if (i > 0 && p < end) *p++ = '-';
            written = snprintf(p, SNPRINTF_SIZE(end - p), "%u", profile->extensions[i]);
            if (written > 0 && written < (end - p)) p += written;
        }
    } else {
        /* Fallback: common extensions */
        written = snprintf(p, SNPRINTF_SIZE(end - p), "0-10-11-13-16-23-35-43-45-51");
        if (written > 0 && written < (end - p)) p += written;
    }

    /* 4. Elliptic Curves - use browser profile's curve list */
    if (p < end) *p++ = ',';
    if (profile && profile->curve_count > 0) {
        for (int i = 0; i < profile->curve_count && p < end; i++) {
            if (i > 0 && p < end) *p++ = '-';
            written = snprintf(p, SNPRINTF_SIZE(end - p), "%u", profile->curves[i]);
            if (written > 0 && written < (end - p)) p += written;
        }
    } else {
        /* Fallback: common curves */
        written = snprintf(p, SNPRINTF_SIZE(end - p), "29-23-24");
        if (written > 0 && written < (end - p)) p += written;
    }

    /* 5. Elliptic Curve Point Formats */
    if (p < end - 2) {
        *p++ = ',';
        *p++ = '0';
        *p = '\0';
    }

    /* Calculate MD5 hash of the JA3 string using EVP interface */
    unsigned char md5_digest[16];  /* MD5 produces 16 bytes */
    unsigned int md5_len = 0;

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        return NULL;
    }

    if (EVP_DigestInit_ex(mdctx, EVP_md5(), NULL) != 1 ||
        EVP_DigestUpdate(mdctx, ja3_string, strlen(ja3_string)) != 1 ||
        EVP_DigestFinal_ex(mdctx, md5_digest, &md5_len) != 1) {
        EVP_MD_CTX_free(mdctx);
        return NULL;
    }
    EVP_MD_CTX_free(mdctx);

    /* Convert MD5 to hex string */
    char *ja3_hash = malloc(33);  /* 32 hex chars + null terminator */
    if (!ja3_hash) {
        return NULL;
    }

    for (int i = 0; i < 16; i++) {
        snprintf(ja3_hash + (i * 2), 3, "%02x", md5_digest[i]);
    }
    ja3_hash[32] = '\0';

    return ja3_hash;
}

/**
 * Configure SSL context TLS version range
 */
int httpmorph_set_tls_version_range(SSL_CTX *ctx, uint16_t min_version, uint16_t max_version) {
    if (!ctx) {
        return -1;
    }

    /* Map our version constants to OpenSSL constants */
    int ssl_min_version = 0;  /* 0 means use default */
    int ssl_max_version = 0;

    /* TLS version mapping (matching browser_profiles.h tls_version_t) */
    switch (min_version) {
        case 0x0301: ssl_min_version = TLS1_VERSION; break;     /* TLS 1.0 */
        case 0x0302: ssl_min_version = TLS1_1_VERSION; break;   /* TLS 1.1 */
        case 0x0303: ssl_min_version = TLS1_2_VERSION; break;   /* TLS 1.2 */
#ifdef TLS1_3_VERSION
        case 0x0304: ssl_min_version = TLS1_3_VERSION; break;   /* TLS 1.3 */
#else
        case 0x0304: ssl_min_version = TLS1_2_VERSION; break;   /* Fallback if TLS 1.3 not available */
#endif
        case 0:      ssl_min_version = 0; break;                /* Default */
        default:     ssl_min_version = TLS1_2_VERSION; break;   /* Fallback to TLS 1.2 */
    }

    switch (max_version) {
        case 0x0301: ssl_max_version = TLS1_VERSION; break;
        case 0x0302: ssl_max_version = TLS1_1_VERSION; break;
        case 0x0303: ssl_max_version = TLS1_2_VERSION; break;
#ifdef TLS1_3_VERSION
        case 0x0304: ssl_max_version = TLS1_3_VERSION; break;
        default:     ssl_max_version = TLS1_3_VERSION; break;   /* Fallback to TLS 1.3 */
#else
        case 0x0304: ssl_max_version = TLS1_2_VERSION; break;   /* Fallback if TLS 1.3 not available */
        default:     ssl_max_version = TLS1_2_VERSION; break;   /* Fallback to TLS 1.2 */
#endif
        case 0:      ssl_max_version = 0; break;                /* Default */
    }

    /* Set TLS version range */
    if (ssl_min_version > 0) {
        if (!SSL_CTX_set_min_proto_version(ctx, ssl_min_version)) {
            return -1;
        }
    }

    if (ssl_max_version > 0) {
        if (!SSL_CTX_set_max_proto_version(ctx, ssl_max_version)) {
            return -1;
        }
    }

    return 0;
}

/**
 * Configure SSL verification mode
 */
int httpmorph_set_ssl_verification(SSL_CTX *ctx, bool verify) {
    if (!ctx) {
        return -1;
    }

    if (verify) {
        /* Enable certificate verification */
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

        /* Set default verification paths (system CA certificates) */
        if (!SSL_CTX_set_default_verify_paths(ctx)) {
            return -1;
        }
    } else {
        /* Disable certificate verification */
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    }

    return 0;
}

#ifdef _WIN32
/**
 * Load CA certificates from Windows Certificate Store into SSL_CTX
 * This is necessary on Windows because SSL_CTX_set_default_verify_paths()
 * doesn't work - Windows stores certificates in the Certificate Store, not files.
 */
int httpmorph_load_windows_ca_certs(SSL_CTX *ctx) {
    if (!ctx) {
        return -1;
    }

    HCERTSTORE hStore = NULL;
    PCCERT_CONTEXT pContext = NULL;
    X509 *x509 = NULL;
    X509_STORE *store = SSL_CTX_get_cert_store(ctx);
    int cert_count = 0;

    if (!store) {
        return -1;
    }

    /* Open the Root CA certificate store */
    hStore = CertOpenSystemStore(0, "ROOT");
    if (!hStore) {
        return -1;
    }

    /* Enumerate all certificates in the store */
    while ((pContext = CertEnumCertificatesInStore(hStore, pContext)) != NULL) {
        /* Convert Windows certificate to OpenSSL X509 format */
        const unsigned char *cert_data = pContext->pbCertEncoded;
        x509 = d2i_X509(NULL, &cert_data, pContext->cbCertEncoded);

        if (x509) {
            /* Add certificate to the SSL_CTX's certificate store */
            if (X509_STORE_add_cert(store, x509) == 1) {
                cert_count++;
            }
            X509_free(x509);
            x509 = NULL;
        }
    }

    /* Also load from CA store (intermediate certificates) */
    if (pContext) {
        CertFreeCertificateContext(pContext);
        pContext = NULL;
    }
    CertCloseStore(hStore, 0);

    hStore = CertOpenSystemStore(0, "CA");
    if (hStore) {
        while ((pContext = CertEnumCertificatesInStore(hStore, pContext)) != NULL) {
            const unsigned char *cert_data = pContext->pbCertEncoded;
            x509 = d2i_X509(NULL, &cert_data, pContext->cbCertEncoded);

            if (x509) {
                if (X509_STORE_add_cert(store, x509) == 1) {
                    cert_count++;
                }
                X509_free(x509);
                x509 = NULL;
            }
        }

        if (pContext) {
            CertFreeCertificateContext(pContext);
        }
        CertCloseStore(hStore, 0);
    }

    /* Return success if we loaded at least one certificate */
    return cert_count > 0 ? 0 : -1;
}
#endif
