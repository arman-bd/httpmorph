/**
 * tls.c - TLS/SSL operations and fingerprinting
 */

#include "internal/tls.h"
#include "internal/util.h"

/* C wrapper for BoringSSL C++ function (defined in boringssl_wrapper.cc) */
extern void httpmorph_set_aes_hw_override(SSL_CTX *ctx, int override_value);

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#pragma comment(lib, "crypt32.lib")
#else
#include <pthread.h>
#endif

/* OpenSSL 1.0.x compatibility */
#if OPENSSL_VERSION_NUMBER < 0x10100000L
/* EVP_MD_CTX functions renamed in 1.1.0 */
#define EVP_MD_CTX_new EVP_MD_CTX_create
#define EVP_MD_CTX_free EVP_MD_CTX_destroy

/* TLS_client_method introduced in 1.1.0, was SSLv23_client_method */
#define TLS_client_method SSLv23_client_method

/* Protocol version setters introduced in 1.1.0 */
static inline int SSL_CTX_set_min_proto_version(SSL_CTX *ctx, int version) {
    /* OpenSSL 1.0.x doesn't support setting min/max versions dynamically */
    /* The best we can do is disable older protocols */
    long opts = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3;
    if (version >= TLS1_1_VERSION) opts |= SSL_OP_NO_TLSv1;
    if (version >= TLS1_2_VERSION) opts |= SSL_OP_NO_TLSv1_1;
    SSL_CTX_set_options(ctx, opts);
    return 1;
}

static inline int SSL_CTX_set_max_proto_version(SSL_CTX *ctx, int version) {
    /* OpenSSL 1.0.x doesn't support setting max version */
    /* We can only disable protocols, not set an upper bound */
    (void)ctx;
    (void)version;
    return 1;
}

/* X25519 curve introduced in 1.1.0 */
#ifndef NID_X25519
#define NID_X25519 0  /* Not available in OpenSSL 1.0.x */
#endif

/* SSL_CTX_set1_groups introduced in 1.1.0, was SSL_CTX_set1_curves_list */
#define SSL_CTX_set1_groups(ctx, glist, glistlen) \
    SSL_CTX_set_ecdh_auto(ctx, 1)
#endif

/* Brotli decompression for compress_certificate extension */
#include <brotli/decode.h>
#include <zlib.h>

/* ====================================================================
 * GLOBAL TLS SESSION CACHE (for async requests without client context)
 * ==================================================================== */

#define GLOBAL_SESSION_CACHE_SIZE 64
#define GLOBAL_SESSION_TTL_SECONDS 300  /* 5 minutes */

typedef struct global_session_entry {
    char host[256];
    uint16_t port;
    SSL_SESSION *session;
    time_t created;
    bool valid;
} global_session_entry_t;

static global_session_entry_t g_session_cache[GLOBAL_SESSION_CACHE_SIZE];
static int g_session_cache_initialized = 0;

#ifdef _WIN32
static CRITICAL_SECTION g_session_cache_mutex;
#else
static pthread_mutex_t g_session_cache_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static void global_session_cache_init(void) {
    if (!g_session_cache_initialized) {
#ifdef _WIN32
        InitializeCriticalSection(&g_session_cache_mutex);
#endif
        memset(g_session_cache, 0, sizeof(g_session_cache));
        g_session_cache_initialized = 1;
    }
}

static void global_session_cache_lock(void) {
#ifdef _WIN32
    EnterCriticalSection(&g_session_cache_mutex);
#else
    pthread_mutex_lock(&g_session_cache_mutex);
#endif
}

static void global_session_cache_unlock(void) {
#ifdef _WIN32
    LeaveCriticalSection(&g_session_cache_mutex);
#else
    pthread_mutex_unlock(&g_session_cache_mutex);
#endif
}

/* Get a TLS session from the global cache */
SSL_SESSION* global_session_cache_get(const char *host, uint16_t port) {
    if (!host) return NULL;

    global_session_cache_init();
    global_session_cache_lock();

    SSL_SESSION *session = NULL;
    time_t now = time(NULL);

    for (int i = 0; i < GLOBAL_SESSION_CACHE_SIZE; i++) {
        if (g_session_cache[i].valid &&
            g_session_cache[i].port == port &&
            strcmp(g_session_cache[i].host, host) == 0) {

            /* Check if entry is expired */
            if (now - g_session_cache[i].created > GLOBAL_SESSION_TTL_SECONDS) {
                /* Expired - invalidate and continue */
                SSL_SESSION_free(g_session_cache[i].session);
                g_session_cache[i].valid = false;
                g_session_cache[i].session = NULL;
                break;
            }

            session = g_session_cache[i].session;
            break;
        }
    }

    global_session_cache_unlock();
    return session;
}

/* Store a TLS session in the global cache */
void global_session_cache_put(const char *host, uint16_t port, SSL_SESSION *session) {
    if (!host || !session) return;

    global_session_cache_init();
    global_session_cache_lock();

    int free_idx = -1;
    int oldest_idx = 0;
    time_t oldest_time = time(NULL);

    /* Look for existing entry or find free/oldest slot */
    for (int i = 0; i < GLOBAL_SESSION_CACHE_SIZE; i++) {
        if (!g_session_cache[i].valid) {
            if (free_idx < 0) free_idx = i;
            continue;
        }

        /* Update existing entry */
        if (g_session_cache[i].port == port &&
            strcmp(g_session_cache[i].host, host) == 0) {
            SSL_SESSION_free(g_session_cache[i].session);
            SSL_SESSION_up_ref(session);
            g_session_cache[i].session = session;
            g_session_cache[i].created = time(NULL);
            global_session_cache_unlock();
            return;
        }

        /* Track oldest for eviction */
        if (g_session_cache[i].created < oldest_time) {
            oldest_time = g_session_cache[i].created;
            oldest_idx = i;
        }
    }

    /* Use free slot or evict oldest */
    int target_idx = (free_idx >= 0) ? free_idx : oldest_idx;

    if (g_session_cache[target_idx].session) {
        SSL_SESSION_free(g_session_cache[target_idx].session);
    }

    strncpy(g_session_cache[target_idx].host, host, sizeof(g_session_cache[target_idx].host) - 1);
    g_session_cache[target_idx].host[sizeof(g_session_cache[target_idx].host) - 1] = '\0';
    g_session_cache[target_idx].port = port;
    SSL_SESSION_up_ref(session);
    g_session_cache[target_idx].session = session;
    g_session_cache[target_idx].created = time(NULL);
    g_session_cache[target_idx].valid = true;

    global_session_cache_unlock();
}

static int cert_decompress_brotli(SSL *ssl, CRYPTO_BUFFER **out,
                                   size_t uncompressed_len,
                                   const uint8_t *in, size_t in_len) {
    (void)ssl;

    /* Allocate buffer for decompressed certificate */
    uint8_t *decompressed = OPENSSL_malloc(uncompressed_len);
    if (!decompressed) {
        return 0;
    }

    /* Decompress using brotli */
    size_t decoded_size = uncompressed_len;
    BrotliDecoderResult result = BrotliDecoderDecompress(
        in_len, in, &decoded_size, decompressed);

    if (result != BROTLI_DECODER_RESULT_SUCCESS || decoded_size != uncompressed_len) {
        OPENSSL_free(decompressed);
        return 0;
    }

    /* Create CRYPTO_BUFFER with decompressed data */
    *out = CRYPTO_BUFFER_new(decompressed, uncompressed_len, NULL);
    OPENSSL_free(decompressed);

    return *out != NULL ? 1 : 0;
}

static int cert_decompress_zlib(SSL *ssl, CRYPTO_BUFFER **out,
                                 size_t uncompressed_len,
                                 const uint8_t *in, size_t in_len) {
    (void)ssl;

    /* Allocate buffer for decompressed certificate */
    uint8_t *decompressed = OPENSSL_malloc(uncompressed_len);
    if (!decompressed) {
        return 0;
    }

    /* Decompress using zlib */
    uLongf dest_len = (uLongf)uncompressed_len;
    int zresult = uncompress(decompressed, &dest_len, in, (uLong)in_len);

    if (zresult != Z_OK || dest_len != uncompressed_len) {
        OPENSSL_free(decompressed);
        return 0;
    }

    /* Create CRYPTO_BUFFER with decompressed data */
    *out = CRYPTO_BUFFER_new(decompressed, uncompressed_len, NULL);
    OPENSSL_free(decompressed);

    return *out != NULL ? 1 : 0;
}

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

    /* Enable GREASE (Generate Random Extensions And Sustain Extensibility)
     * Chrome sends GREASE values in ciphers, extensions, groups, and versions
     * to ensure servers handle unknown values gracefully */
    if (profile->use_grease) {
        SSL_CTX_set_grease_enabled(ctx, 1);
    }

    /* Enable extension permutation to match Chrome's behavior.
     * Chrome randomizes extension order in ClientHello for each connection.
     * Note: JA4 sorts extensions alphabetically, so this doesn't affect JA4 fingerprint. */
    SSL_CTX_set_permute_extensions(ctx, 1);

    /* Check if compress_certificate (27) is in the profile's extension list */
    bool has_compress_cert = false;
    for (int i = 0; i < profile->extension_count; i++) {
        if (profile->extensions[i] == 27) {
            has_compress_cert = true;
            break;
        }
    }

    /* Enable compress_certificate extension (0x001b) only if profile includes it.
     * Chrome 143 ONLY advertises brotli (2) in the compress_certificate extension.
     * We must match this exactly for fingerprint accuracy - only register brotli.
     * Servers will only send brotli-compressed certs since that's what we advertise. */
    if (has_compress_cert) {
        SSL_CTX_add_cert_compression_alg(ctx, TLSEXT_cert_compression_brotli, NULL, cert_decompress_brotli);
    }

    /* Force AES hardware preference to match Chrome's cipher order (AES-GCM before ChaCha20)
     * This prevents BoringSSL from reordering ciphers based on ARM vs Intel CPU capabilities */
    httpmorph_set_aes_hw_override(ctx, 1);

    /* Build TLS 1.3 ciphersuites (preserves exact order) */
    char tls13_ciphers[512] = {0};
    char *p13 = tls13_ciphers;

    /* Build TLS 1.2 cipher list */
    char tls12_ciphers[2048] = {0};
    char *p12 = tls12_ciphers;

    for (int i = 0; i < profile->cipher_suite_count; i++) {
        uint16_t cs = profile->cipher_suites[i];
        const char *name = NULL;
        int is_tls13 = 0;

        /* Map cipher suite code to BoringSSL name */
        switch (cs) {
            /* TLS 1.3 cipher suites */
            case 0x1301: name = "TLS_AES_128_GCM_SHA256"; is_tls13 = 1; break;
            case 0x1302: name = "TLS_AES_256_GCM_SHA384"; is_tls13 = 1; break;
            case 0x1303: name = "TLS_CHACHA20_POLY1305_SHA256"; is_tls13 = 1; break;
            /* TLS 1.2 ECDHE cipher suites */
            case 0xc02b: name = "ECDHE-ECDSA-AES128-GCM-SHA256"; break;
            case 0xc02f: name = "ECDHE-RSA-AES128-GCM-SHA256"; break;
            case 0xc02c: name = "ECDHE-ECDSA-AES256-GCM-SHA384"; break;
            case 0xc030: name = "ECDHE-RSA-AES256-GCM-SHA384"; break;
            case 0xc013: name = "ECDHE-RSA-AES128-SHA"; break;
            case 0xc014: name = "ECDHE-RSA-AES256-SHA"; break;
            case 0xcca9: name = "ECDHE-ECDSA-CHACHA20-POLY1305"; break;
            case 0xcca8: name = "ECDHE-RSA-CHACHA20-POLY1305"; break;
            /* TLS 1.2 RSA cipher suites */
            case 0x002f: name = "AES128-SHA"; break;
            case 0x0035: name = "AES256-SHA"; break;
            case 0x009c: name = "AES128-GCM-SHA256"; break;
            case 0x009d: name = "AES256-GCM-SHA384"; break;
            default: continue;  /* Skip unsupported */
        }

        if (name) {
            size_t name_len = strlen(name);
            if (is_tls13) {
                /* Check bounds before adding to TLS 1.3 buffer */
                size_t space_needed = name_len + (p13 != tls13_ciphers ? 1 : 0);  /* +1 for ':' */
                if ((size_t)(p13 - tls13_ciphers) + space_needed >= sizeof(tls13_ciphers)) {
                    continue;  /* Skip this cipher - would overflow */
                }
                if (p13 != tls13_ciphers) *p13++ = ':';
                memcpy(p13, name, name_len);
                p13 += name_len;
                *p13 = '\0';  /* Ensure null termination */
            } else {
                /* Check bounds before adding to TLS 1.2 buffer */
                size_t space_needed = name_len + (p12 != tls12_ciphers ? 1 : 0);  /* +1 for ':' */
                if ((size_t)(p12 - tls12_ciphers) + space_needed >= sizeof(tls12_ciphers)) {
                    continue;  /* Skip this cipher - would overflow */
                }
                if (p12 != tls12_ciphers) *p12++ = ':';
                memcpy(p12, name, name_len);
                p12 += name_len;
                *p12 = '\0';  /* Ensure null termination */
            }
        }
    }

    /* Combine TLS 1.3 and TLS 1.2 ciphers with TLS 1.3 first */
    char combined_ciphers[2560] = {0};
    if (strlen(tls13_ciphers) > 0 && strlen(tls12_ciphers) > 0) {
        snprintf(combined_ciphers, sizeof(combined_ciphers), "%s:%s", tls13_ciphers, tls12_ciphers);
    } else if (strlen(tls13_ciphers) > 0) {
        snprintf(combined_ciphers, sizeof(combined_ciphers), "%s", tls13_ciphers);
    } else if (strlen(tls12_ciphers) > 0) {
        snprintf(combined_ciphers, sizeof(combined_ciphers), "%s", tls12_ciphers);
    }

    /* Use strict cipher list to preserve exact order */
    if (strlen(combined_ciphers) > 0) {
        if (SSL_CTX_set_strict_cipher_list(ctx, combined_ciphers) != 1) {
            return -1;
        }
    }

    /* Set supported curves */
    if (profile->curve_count > 0) {
        int nids[MAX_CURVES];
        int nid_count = 0;

        for (int i = 0; i < profile->curve_count && nid_count < MAX_CURVES; i++) {
            int nid = -1;
            switch (profile->curves[i]) {
                case 0x11ec: nid = NID_X25519MLKEM768; break;  /* X25519MLKEM768 (post-quantum hybrid) */
                case 0x001d: nid = NID_X25519; break;
                case 0x0017: nid = NID_X9_62_prime256v1; break;  /* secp256r1 */
                case 0x0018: nid = NID_secp384r1; break;
                case 0x0019: nid = NID_secp521r1; break;
                default: continue;
            }

            /* Skip if NID is invalid or unsupported (0 for X25519 on OpenSSL 1.0.x) */
            if (nid > 0) {
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

    /* Note: OCSP stapling disabled because BoringSSL adds both status_request (0x0005)
     * AND status_request_v2 (0x0015), but Chrome 142's fingerprint only includes 0x0005.
     * The status_request extension appears to be added by BoringSSL automatically. */
    // SSL_CTX_enable_ocsp_stapling(ctx);

    /* Check if signed_certificate_timestamp (18) is in the profile's extension list */
    bool has_sct = false;
    for (int i = 0; i < profile->extension_count; i++) {
        if (profile->extensions[i] == 18) {
            has_sct = true;
            break;
        }
    }

    /* Enable signed_certificate_timestamp extension (0x0012) only if profile includes it */
    if (has_sct) {
        SSL_CTX_enable_signed_cert_timestamps(ctx);
    }

    /* Configure signature algorithms (advertised in ClientHello) */
    if (profile->signature_algorithm_count > 0) {
        /* Use verify_algorithm_prefs which controls what's advertised in ClientHello */
        SSL_CTX_set_verify_algorithm_prefs(ctx,
            profile->signature_algorithms,
            profile->signature_algorithm_count);
    }

    /* Note: application_settings (0x44cd/ALPS) and encrypted_client_hello
     * (0xfe0d/ECH) require per-connection setup. They will be enabled
     * per-SSL object in httpmorph_tls_connect(). */

    return 0;
}

/* ==================================================================
 * TLS SESSION CACHE
 * ================================================================== */

#ifndef _WIN32
#include <pthread.h>
#endif

/**
 * Store a TLS session in the client's cache
 * Note: We increment the reference count rather than duplicating,
 * since BoringSSL doesn't have SSL_SESSION_dup.
 */
void httpmorph_session_cache_put(httpmorph_client_t *client, const char *host,
                                   uint16_t port, SSL_SESSION *session) {
    if (!client || !host || !session) return;

#ifdef _WIN32
    EnterCriticalSection(&client->session_cache_mutex);
#else
    pthread_mutex_lock(&client->session_cache_mutex);
#endif

    /* Look for existing entry to update */
    for (int i = 0; i < client->session_cache_count; i++) {
        if (client->session_cache[i].port == port &&
            strcmp(client->session_cache[i].host, host) == 0) {
            /* Replace existing session */
            SSL_SESSION_free(client->session_cache[i].session);
            SSL_SESSION_up_ref(session);  /* Increment ref count */
            client->session_cache[i].session = session;
            client->session_cache[i].created = time(NULL);
            goto unlock;
        }
    }

    /* Add new entry */
    if (client->session_cache_count < MAX_SESSION_CACHE_ENTRIES) {
        int idx = client->session_cache_count++;
        strncpy(client->session_cache[idx].host, host, sizeof(client->session_cache[idx].host) - 1);
        client->session_cache[idx].host[sizeof(client->session_cache[idx].host) - 1] = '\0';
        client->session_cache[idx].port = port;
        SSL_SESSION_up_ref(session);  /* Increment ref count */
        client->session_cache[idx].session = session;
        client->session_cache[idx].created = time(NULL);
    } else {
        /* Cache full - replace oldest entry */
        int oldest_idx = 0;
        time_t oldest_time = client->session_cache[0].created;
        for (int i = 1; i < MAX_SESSION_CACHE_ENTRIES; i++) {
            if (client->session_cache[i].created < oldest_time) {
                oldest_time = client->session_cache[i].created;
                oldest_idx = i;
            }
        }
        SSL_SESSION_free(client->session_cache[oldest_idx].session);
        strncpy(client->session_cache[oldest_idx].host, host, sizeof(client->session_cache[oldest_idx].host) - 1);
        client->session_cache[oldest_idx].host[sizeof(client->session_cache[oldest_idx].host) - 1] = '\0';
        client->session_cache[oldest_idx].port = port;
        SSL_SESSION_up_ref(session);  /* Increment ref count */
        client->session_cache[oldest_idx].session = session;
        client->session_cache[oldest_idx].created = time(NULL);
    }

unlock:
#ifdef _WIN32
    LeaveCriticalSection(&client->session_cache_mutex);
#else
    pthread_mutex_unlock(&client->session_cache_mutex);
#endif
}

/**
 * Get a TLS session from the client's cache
 * Returns NULL if not found, or a reference to the cached session.
 * Caller should NOT free the returned session - it's owned by the cache.
 */
SSL_SESSION* httpmorph_session_cache_get(httpmorph_client_t *client, const char *host, uint16_t port) {
    if (!client || !host) return NULL;

    SSL_SESSION *session = NULL;

#ifdef _WIN32
    EnterCriticalSection(&client->session_cache_mutex);
#else
    pthread_mutex_lock(&client->session_cache_mutex);
#endif

    for (int i = 0; i < client->session_cache_count; i++) {
        if (client->session_cache[i].port == port &&
            strcmp(client->session_cache[i].host, host) == 0) {
            /* Return the cached session directly - caller should not free it */
            session = client->session_cache[i].session;
            break;
        }
    }

#ifdef _WIN32
    LeaveCriticalSection(&client->session_cache_mutex);
#else
    pthread_mutex_unlock(&client->session_cache_mutex);
#endif

    return session;
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

    /* Check which extensions the profile includes */
    bool has_ech = false;
    bool has_alps = false;
    bool has_ocsp = false;
    if (browser_profile) {
        for (int i = 0; i < browser_profile->extension_count; i++) {
            uint16_t ext = browser_profile->extensions[i];
            if (ext == 65037) has_ech = true;   /* encrypted_client_hello */
            if (ext == 17613 || ext == 17513) has_alps = true;  /* application_settings (NEW=17613/0x44cd, OLD=17513/0x4469) */
            if (ext == 5) has_ocsp = true;      /* status_request */
        }
    }

    /* Enable/disable ECH grease for encrypted_client_hello extension (0xfe0d) based on profile */
    SSL_set_enable_ech_grease(ssl, has_ech ? 1 : 0);

    /* Enable OCSP stapling for status_request extension (0x0005) only if profile includes it */
    if (has_ocsp) {
        SSL_enable_ocsp_stapling(ssl);
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

            /* Enable ALPS (application_settings extension 0x44cd) only if profile includes it.
             * Chrome 143 ONLY advertises "h2" in application_settings, NOT "http/1.1".
             * We must match this exactly for fingerprint accuracy. */
            if (has_alps && http2_enabled) {
                /* Only add ALPS for "h2" protocol - Chrome doesn't advertise http/1.1 in ALPS */
                SSL_add_application_settings(ssl,
                    (const uint8_t *)"h2", 2,
                    (const uint8_t *)"", 0);
            }
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
        /* Get detailed SSL error */
        unsigned long err = ERR_get_error();
        if (err != 0) {
            char err_buf[256];
            ERR_error_string_n(err, err_buf, sizeof(err_buf));
            fprintf(stderr, "TLS handshake error: %s (SSL error code: %d)\n", err_buf, ssl_err);
        } else {
            fprintf(stderr, "TLS handshake failed with SSL error code: %d\n", ssl_err);
        }
        SSL_free(ssl);
        return NULL;
    }

    *tls_time_us = httpmorph_get_time_us() - start_time;
    return ssl;
}

/**
 * Establish TLS connection with session caching support
 * Uses cached TLS sessions to speed up repeated connections to the same host
 */
SSL* httpmorph_tls_connect_cached(httpmorph_client_t *client, int sockfd,
                                   const char *hostname, uint16_t port,
                                   bool http2_enabled, bool verify_cert,
                                   uint64_t *tls_time) {
    if (!client || !hostname) {
        return NULL;
    }

    uint64_t start_time = httpmorph_get_time_us();

    SSL *ssl = SSL_new(client->ssl_ctx);
    if (!ssl) {
        return NULL;
    }

    /* Try to reuse a cached session for faster resumption */
    SSL_SESSION *cached_session = httpmorph_session_cache_get(client, hostname, port);
    if (cached_session) {
        SSL_set_session(ssl, cached_session);
        /* Don't free - cache owns the session */
    }

    /* Check which extensions the profile includes */
    const browser_profile_t *browser_profile = client->browser_profile;
    bool has_ech = false;
    bool has_alps = false;
    bool has_ocsp = false;
    if (browser_profile) {
        for (int i = 0; i < browser_profile->extension_count; i++) {
            uint16_t ext = browser_profile->extensions[i];
            if (ext == 65037) has_ech = true;
            if (ext == 17613 || ext == 17513) has_alps = true;
            if (ext == 5) has_ocsp = true;
        }
    }

    /* Enable/disable ECH grease */
    SSL_set_enable_ech_grease(ssl, has_ech ? 1 : 0);

    /* Enable OCSP stapling if profile includes it */
    if (has_ocsp) {
        SSL_enable_ocsp_stapling(ssl);
    }

    /* Set SSL verification mode */
    if (verify_cert) {
        SSL_set_verify(ssl, SSL_VERIFY_PEER, NULL);
    } else {
        SSL_set_verify(ssl, SSL_VERIFY_NONE, NULL);
    }

    /* Set ALPN protocols */
    if (browser_profile && browser_profile->alpn_protocol_count > 0) {
        unsigned char alpn_list[256];
        unsigned char *alpn_p = alpn_list;

        for (int i = 0; i < browser_profile->alpn_protocol_count; i++) {
            if (!http2_enabled && strcmp(browser_profile->alpn_protocols[i], "h2") == 0) {
                continue;
            }

            size_t len = strlen(browser_profile->alpn_protocols[i]);
            *alpn_p++ = (unsigned char)len;
            memcpy(alpn_p, browser_profile->alpn_protocols[i], len);
            alpn_p += len;
        }

        if (alpn_p > alpn_list) {
            SSL_set_alpn_protos(ssl, alpn_list, alpn_p - alpn_list);

            if (has_alps && http2_enabled) {
                SSL_add_application_settings(ssl,
                    (const uint8_t *)"h2", 2,
                    (const uint8_t *)"", 0);
            }
        }
    }

    /* Set SNI hostname */
    SSL_set_tlsext_host_name(ssl, hostname);

    /* Attach to socket */
    if (SSL_set_fd(ssl, sockfd) != 1) {
        SSL_free(ssl);
        return NULL;
    }

    /* Perform TLS handshake */
    int ret;
    int ssl_err;
    uint64_t handshake_timeout_us = 30000000;
    uint64_t deadline = start_time + handshake_timeout_us;

    while (1) {
        ret = SSL_connect(ssl);
        if (ret == 1) {
            break;  /* Success */
        }

        ssl_err = SSL_get_error(ssl, ret);

        if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
            uint64_t now = httpmorph_get_time_us();
            if (now >= deadline) {
                SSL_free(ssl);
                return NULL;  /* Timeout */
            }

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
                return NULL;
            }
            continue;
        }

        /* Handshake failed */
        SSL_free(ssl);
        return NULL;
    }

    /* Cache the session for future connections */
    SSL_SESSION *new_session = SSL_get_session(ssl);
    if (new_session) {
        httpmorph_session_cache_put(client, hostname, port, new_session);
    }

    *tls_time = httpmorph_get_time_us() - start_time;
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
