/**
 * browser_profiles.c - Browser TLS/HTTP fingerprint profiles implementation
 */

#ifndef _WIN32
    #define _POSIX_C_SOURCE 200809L
#endif

#include "browser_profiles.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Platform-specific headers */
#ifdef _WIN32
    #define strcasecmp _stricmp
#else
    #include <strings.h>  /* for strcasecmp */
#endif

/* Chrome 131 Profile */
const browser_profile_t PROFILE_CHROME_131 = {
    .name = "Chrome 131",
    .version = "131.0.6778.109",
    .user_agent = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36",

    .min_tls_version = TLS_VERSION_1_2,
    .max_tls_version = TLS_VERSION_1_3,

    .cipher_suites = {
        0x1301,  /* TLS_AES_128_GCM_SHA256 */
        0x1302,  /* TLS_AES_256_GCM_SHA384 */
        0x1303,  /* TLS_CHACHA20_POLY1305_SHA256 */
        0xc02b,  /* TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256 */
        0xc02f,  /* TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256 */
        0xc02c,  /* TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384 */
        0xc030,  /* TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384 */
        0xcca9,  /* TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256 */
        0xcca8,  /* TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256 */
    },
    .cipher_suite_count = 9,

    .extensions = {
        0,      /* server_name */
        10,     /* supported_groups */
        11,     /* ec_point_formats */
        13,     /* signature_algorithms */
        16,     /* application_layer_protocol_negotiation */
        18,     /* signed_certificate_timestamp */
        21,     /* padding */
        23,     /* extended_master_secret */
        27,     /* compress_certificate */
        35,     /* session_ticket */
        43,     /* supported_versions */
        45,     /* psk_key_exchange_modes */
        51,     /* key_share */
    },
    .extension_count = 13,

    .curves = {
        0x001d,  /* X25519 */
        0x0017,  /* secp256r1 */
        0x0018,  /* secp384r1 */
    },
    .curve_count = 3,

    .signature_algorithms = {
        0x0403,  /* ecdsa_secp256r1_sha256 */
        0x0804,  /* rsa_pss_rsae_sha256 */
        0x0401,  /* rsa_pkcs1_sha256 */
        0x0503,  /* ecdsa_secp384r1_sha384 */
        0x0805,  /* rsa_pss_rsae_sha384 */
        0x0501,  /* rsa_pkcs1_sha384 */
        0x0806,  /* rsa_pss_rsae_sha512 */
        0x0601,  /* rsa_pkcs1_sha512 */
    },
    .signature_algorithm_count = 8,

    .alpn_protocols = {"h2", "http/1.1"},
    .alpn_protocol_count = 2,

    .use_grease = true,
    .grease_cipher = 0x0a0a,
    .grease_extension = 0x0a0a,
    .grease_group = 0x0a0a,

    .http2 = {
        .settings = {
            {1, 65536},    /* SETTINGS_HEADER_TABLE_SIZE */
            {2, 0},        /* SETTINGS_ENABLE_PUSH */
            {3, 1000},     /* SETTINGS_MAX_CONCURRENT_STREAMS */
            {4, 6291456},  /* SETTINGS_INITIAL_WINDOW_SIZE */
            {5, 16384},    /* SETTINGS_MAX_FRAME_SIZE */
            {6, 262144},   /* SETTINGS_MAX_HEADER_LIST_SIZE */
        },
        .setting_count = 6,
        .window_update = 15663105,
    },

    .ja3_hash = "cd08e31494f9531f560d64c695473da9",  /* MD5 of JA3 string */
};

/* Chrome 124 Profile (older version) */
const browser_profile_t PROFILE_CHROME_124 = {
    .name = "Chrome 124",
    .version = "124.0.6367.207",
    .user_agent = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36",

    .min_tls_version = TLS_VERSION_1_2,
    .max_tls_version = TLS_VERSION_1_3,

    .cipher_suites = {
        0x1301, 0x1302, 0x1303,
        0xc02b, 0xc02f, 0xc02c, 0xc030,
        0xcca9, 0xcca8,
    },
    .cipher_suite_count = 9,

    .curves = {0x001d, 0x0017, 0x0018},
    .curve_count = 3,

    .alpn_protocols = {"h2", "http/1.1"},
    .alpn_protocol_count = 2,

    .use_grease = true,
};

/* Firefox 122 Profile */
const browser_profile_t PROFILE_FIREFOX_122 = {
    .name = "Firefox 122",
    .version = "122.0",
    .user_agent = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:122.0) Gecko/20100101 Firefox/122.0",

    .min_tls_version = TLS_VERSION_1_2,
    .max_tls_version = TLS_VERSION_1_3,

    .cipher_suites = {
        0x1301, 0x1303, 0x1302,
        0xc02b, 0xc02f, 0xc02c, 0xc030,
        0xcca9, 0xcca8,
    },
    .cipher_suite_count = 9,

    .curves = {0x001d, 0x0017, 0x0018, 0x0019},
    .curve_count = 4,

    .alpn_protocols = {"h2", "http/1.1"},
    .alpn_protocol_count = 2,

    .use_grease = false,
};

/* Safari 17 Profile */
const browser_profile_t PROFILE_SAFARI_17 = {
    .name = "Safari 17",
    .version = "17.0",
    .user_agent = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Safari/605.1.15",

    .min_tls_version = TLS_VERSION_1_2,
    .max_tls_version = TLS_VERSION_1_3,

    .cipher_suites = {
        0x1301, 0x1302, 0x1303,
        0xc02c, 0xc030, 0xc02b, 0xc02f,
        0xcca9, 0xcca8,
    },
    .cipher_suite_count = 9,

    .curves = {0x001d, 0x0017, 0x0018},
    .curve_count = 3,

    .alpn_protocols = {"h2", "http/1.1"},
    .alpn_protocol_count = 2,

    .use_grease = false,
};

/* Edge 122 Profile */
const browser_profile_t PROFILE_EDGE_122 = {
    .name = "Edge 122",
    .version = "122.0.2365.92",
    .user_agent = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36 Edg/122.0.0.0",

    .min_tls_version = TLS_VERSION_1_2,
    .max_tls_version = TLS_VERSION_1_3,

    .cipher_suites = {
        0x1301, 0x1302, 0x1303,
        0xc02b, 0xc02f, 0xc02c, 0xc030,
        0xcca9, 0xcca8,
    },
    .cipher_suite_count = 9,

    .curves = {0x001d, 0x0017, 0x0018},
    .curve_count = 3,

    .alpn_protocols = {"h2", "http/1.1"},
    .alpn_protocol_count = 2,

    .use_grease = true,
};

/* Profile database */
static const browser_profile_t *profiles[] = {
    &PROFILE_CHROME_131,
    &PROFILE_CHROME_124,
    &PROFILE_FIREFOX_122,
    &PROFILE_SAFARI_17,
    &PROFILE_EDGE_122,
};

static const int profile_count = sizeof(profiles) / sizeof(profiles[0]);

/**
 * Get profile by name
 */
const browser_profile_t* browser_profile_get(const char *name) {
    if (!name) {
        return NULL;
    }

    for (int i = 0; i < profile_count; i++) {
        if (strcmp(profiles[i]->name, name) == 0) {
            return profiles[i];
        }
    }

    return NULL;
}

/**
 * Get random profile
 */
const browser_profile_t* browser_profile_random(void) {
    static bool seeded = false;
    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = true;
    }

    int index = rand() % profile_count;
    return profiles[index];
}

/**
 * Get profile by browser type
 */
const browser_profile_t* browser_profile_by_type(const char *browser_type) {
    if (!browser_type) {
        return &PROFILE_CHROME_131;
    }

    if (strcasecmp(browser_type, "chrome") == 0) {
        return &PROFILE_CHROME_131;
    } else if (strcasecmp(browser_type, "firefox") == 0) {
        return &PROFILE_FIREFOX_122;
    } else if (strcasecmp(browser_type, "safari") == 0) {
        return &PROFILE_SAFARI_17;
    } else if (strcasecmp(browser_type, "edge") == 0) {
        return &PROFILE_EDGE_122;
    }

    return &PROFILE_CHROME_131;
}

/**
 * List all available profiles
 */
const char** browser_profile_list(int *count) {
    if (count) {
        *count = profile_count;
    }

    static const char *names[5];
    for (int i = 0; i < profile_count; i++) {
        names[i] = profiles[i]->name;
    }

    return names;
}

/**
 * Generate dynamic profile based on real browser with variations
 */
browser_profile_t* browser_profile_generate_variant(const browser_profile_t *base) {
    if (!base) {
        return NULL;
    }

    browser_profile_t *variant = malloc(sizeof(browser_profile_t));
    if (!variant) {
        return NULL;
    }

    /* Copy base profile */
    memcpy(variant, base, sizeof(browser_profile_t));

    /* Add randomization to make each variant slightly different */

    /* Randomize GREASE values (GRE ASE - Generate Random Extensions And Sustain Extensibility)
     * GREASE values should be different for each connection */
    if (variant->use_grease) {
        /* GREASE cipher suites: 0x0a0a, 0x1a1a, 0x2a2a, etc. */
        const uint16_t grease_values[] = {
            0x0a0a, 0x1a1a, 0x2a2a, 0x3a3a, 0x4a4a,
            0x5a5a, 0x6a6a, 0x7a7a, 0x8a8a, 0x9a9a,
            0xaaaa, 0xbaba, 0xcaca, 0xdada, 0xeaea, 0xfafa
        };
        int num_grease = sizeof(grease_values) / sizeof(grease_values[0]);

        /* Pick random GREASE values */
        variant->grease_cipher = grease_values[rand() % num_grease];
        variant->grease_extension = grease_values[rand() % num_grease];
        variant->grease_group = grease_values[rand() % num_grease];
    }

    /* Minor randomization of cipher suite order (swap adjacent non-critical ciphers)
     * Only randomize the middle ciphers, keep first 2 and last 2 fixed for stability */
    if (variant->cipher_suite_count > 6) {
        for (int i = 2; i < variant->cipher_suite_count - 2; i++) {
            /* 30% chance to swap with next cipher */
            if ((rand() % 100) < 30 && i + 1 < variant->cipher_suite_count - 2) {
                uint16_t temp = variant->cipher_suites[i];
                variant->cipher_suites[i] = variant->cipher_suites[i + 1];
                variant->cipher_suites[i + 1] = temp;
                i++;  /* Skip next to avoid double-swapping */
            }
        }
    }

    /* Randomize extension order slightly (swap adjacent extensions, not critical ones)
     * Keep server_name (0), supported_versions (43), and key_share (51) in their positions */
    if (variant->extension_count > 4) {
        for (int i = 1; i < variant->extension_count - 1; i++) {
            uint16_t ext = variant->extensions[i];

            /* Don't move critical extensions */
            if (ext == TLS_EXT_SERVER_NAME ||
                ext == TLS_EXT_SUPPORTED_VERSIONS ||
                ext == TLS_EXT_KEY_SHARE) {
                continue;
            }

            /* 25% chance to swap with next non-critical extension */
            if ((rand() % 100) < 25 && i + 1 < variant->extension_count - 1) {
                uint16_t next_ext = variant->extensions[i + 1];

                /* Check if next is also non-critical */
                if (next_ext != TLS_EXT_SERVER_NAME &&
                    next_ext != TLS_EXT_SUPPORTED_VERSIONS &&
                    next_ext != TLS_EXT_KEY_SHARE) {

                    variant->extensions[i] = next_ext;
                    variant->extensions[i + 1] = ext;
                    i++;  /* Skip next */
                }
            }
        }
    }

    /* Note: We intentionally don't invalidate the ja3_hash here because:
     * 1. Minor cipher/extension reordering should stay within expected variance
     * 2. GREASE values are supposed to change per-connection
     * 3. Real browsers show similar variance in fingerprints
     * The precomputed JA3 represents the "base" fingerprint family */

    return variant;
}

/**
 * Destroy a generated profile
 */
void browser_profile_destroy(browser_profile_t *profile) {
    free(profile);
}
