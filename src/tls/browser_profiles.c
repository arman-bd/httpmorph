/**
 * browser_profiles.c - Browser TLS/HTTP fingerprint profiles implementation
 *
 * Supported Chrome versions: 127-143
 * All profiles produce EXACT JA4 fingerprint matches
 *
 * JA4: t13d1516h2_8daaf6152771_d8a2da3f94cd
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

/*
 * Chrome TLS fingerprint (Chrome 127-143):
 *   - ALPS NEW extension (17613/0x44cd)
 *   - Encrypted Client Hello (65037/0xfe0d)
 *   - X25519MLKEM768 (0x11ec/4588) hybrid post-quantum curve
 *   - TLS 1.3 with modern cipher suites
 *   - GREASE enabled for forward compatibility
 *
 * JA4: t13d1516h2_8daaf6152771_d8a2da3f94cd
 * Extensions: 0005,000a,000b,000d,0012,0017,001b,0023,002b,002d,0033,44cd,fe0d,ff01
 */

/* ==========================================================================
 * CHROME 127-143: ALPS NEW (17613), ECH, Post-quantum X25519MLKEM768
 * JA4: t13d1516h2_8daaf6152771_d8a2da3f94cd
 * Extensions: 0005,000a,000b,000d,0012,0017,001b,0023,002b,002d,0033,44cd,fe0d,ff01
 * ========================================================================== */

#define CHROME_127_143_PROFILE(ver, build) \
const browser_profile_t PROFILE_CHROME_##ver = { \
    .name = "chrome" #ver, \
    .version = #ver ".0." #build, \
    .user_agent = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/" #ver ".0.0.0 Safari/537.36", \
    .user_agent_windows = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/" #ver ".0.0.0 Safari/537.36", \
    .user_agent_linux = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/" #ver ".0.0.0 Safari/537.36", \
    .min_tls_version = TLS_VERSION_1_2, \
    .max_tls_version = TLS_VERSION_1_3, \
    .cipher_suites = { \
        0x1301, 0x1302, 0x1303, 0xc02b, 0xc02f, 0xc02c, 0xc030, \
        0xcca9, 0xcca8, 0xc013, 0xc014, 0x009c, 0x009d, 0x002f, 0x0035, \
    }, \
    .cipher_suite_count = 15, \
    .extensions = { \
        5, 10, 11, 13, 18, 23, 27, 35, 43, 45, 51, 17613, 65037, 65281, \
    }, \
    .extension_count = 14, \
    .curves = { \
        0x11ec, 0x001d, 0x0017, 0x0018, \
    }, \
    .curve_count = 4, \
    .signature_algorithms = { 0x0403, 0x0804, 0x0401, 0x0503, 0x0805, 0x0501, 0x0806, 0x0601 }, \
    .signature_algorithm_count = 8, \
    .alpn_protocols = {"h2", "http/1.1"}, \
    .alpn_protocol_count = 2, \
    .use_grease = true, \
    .grease_cipher = 0x0a0a, \
    .grease_extension = 0x0a0a, \
    .grease_group = 0x0a0a, \
    .http2 = { \
        .settings = { {1, 65536}, {2, 0}, {4, 6291456}, {6, 262144} }, \
        .setting_count = 4, \
        .window_update = 15663105, \
    }, \
    .ja3_hash = "ad39201d5fec29cb6a0bfe632d59781b", \
};

CHROME_127_143_PROFILE(127, 6533.72)
CHROME_127_143_PROFILE(128, 6613.84)
CHROME_127_143_PROFILE(129, 6668.58)
CHROME_127_143_PROFILE(130, 6723.58)
CHROME_127_143_PROFILE(131, 6778.85)
CHROME_127_143_PROFILE(132, 6834.83)
CHROME_127_143_PROFILE(133, 6890.0)
CHROME_127_143_PROFILE(134, 6945.0)
CHROME_127_143_PROFILE(135, 7000.0)
CHROME_127_143_PROFILE(136, 7055.0)
CHROME_127_143_PROFILE(137, 7110.0)
CHROME_127_143_PROFILE(138, 7165.0)
CHROME_127_143_PROFILE(139, 7220.0)
CHROME_127_143_PROFILE(140, 7275.0)
CHROME_127_143_PROFILE(141, 7330.0)
CHROME_127_143_PROFILE(142, 7385.0)

/* Chrome 143 - Current Chrome version with exact fingerprint from source_chrome.json */
const browser_profile_t PROFILE_CHROME_143 = {
    .name = "chrome143",
    .version = "143.0.0.0",
    .user_agent = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/143.0.0.0 Safari/537.36",
    .user_agent_windows = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/143.0.0.0 Safari/537.36",
    .user_agent_linux = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/143.0.0.0 Safari/537.36",

    .min_tls_version = TLS_VERSION_1_2,
    .max_tls_version = TLS_VERSION_1_3,

    /* From source_chrome.json JA3N: 772,4865-4866-4867-49195-49199-49196-49200-52393-52392-49171-49172-156-157-47-53 */
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
        0xc013,  /* TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA */
        0xc014,  /* TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA */
        0x009c,  /* TLS_RSA_WITH_AES_128_GCM_SHA256 */
        0x009d,  /* TLS_RSA_WITH_AES_256_GCM_SHA384 */
        0x002f,  /* TLS_RSA_WITH_AES_128_CBC_SHA */
        0x0035,  /* TLS_RSA_WITH_AES_256_CBC_SHA */
    },
    .cipher_suite_count = 15,

    /* From source_chrome.json JA3N: 0-5-10-11-13-16-18-23-27-35-43-45-51-17613-65037-65281 */
    .extensions = {
        5,      /* 0x0005 - status_request (OCSP) */
        10,     /* 0x000a - supported_groups */
        11,     /* 0x000b - ec_point_formats */
        13,     /* 0x000d - signature_algorithms */
        18,     /* 0x0012 - signed_certificate_timestamp */
        23,     /* 0x0017 - extended_master_secret */
        27,     /* 0x001b - compress_certificate */
        35,     /* 0x0023 - session_ticket */
        43,     /* 0x002b - supported_versions */
        45,     /* 0x002d - psk_key_exchange_modes */
        51,     /* 0x0033 - key_share */
        17613,  /* 0x44cd - application_settings (ALPS) */
        65037,  /* 0xfe0d - encrypted_client_hello (ECH) */
        65281,  /* 0xff01 - renegotiation_info */
    },
    .extension_count = 14,

    /* From source_chrome.json: 4588-29-23-24 */
    .curves = {
        0x11ec,  /* X25519MLKEM768 (post-quantum hybrid) */
        0x001d,  /* X25519 */
        0x0017,  /* secp256r1 */
        0x0018,  /* secp384r1 */
    },
    .curve_count = 4,

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
        /* Chrome sends only 4 settings: 1,2,4,6 (no settings 3 or 5)
         * Akamai fingerprint: 1:65536;2:0;4:6291456;6:262144 */
        .settings = {
            {1, 65536},    /* SETTINGS_HEADER_TABLE_SIZE */
            {2, 0},        /* SETTINGS_ENABLE_PUSH */
            {4, 6291456},  /* SETTINGS_INITIAL_WINDOW_SIZE */
            {6, 262144},   /* SETTINGS_MAX_HEADER_LIST_SIZE */
        },
        .setting_count = 4,
        .window_update = 15663105,
    },

    .ja3_hash = "ad39201d5fec29cb6a0bfe632d59781b",
};


/* Profile database - Chrome 127-143 (all with exact JA4 fingerprint matches) */
static const browser_profile_t *profiles[] = {
    &PROFILE_CHROME_127,
    &PROFILE_CHROME_128,
    &PROFILE_CHROME_129,
    &PROFILE_CHROME_130,
    &PROFILE_CHROME_131,
    &PROFILE_CHROME_132,
    &PROFILE_CHROME_133,
    &PROFILE_CHROME_134,
    &PROFILE_CHROME_135,
    &PROFILE_CHROME_136,
    &PROFILE_CHROME_137,
    &PROFILE_CHROME_138,
    &PROFILE_CHROME_139,
    &PROFILE_CHROME_140,
    &PROFILE_CHROME_141,
    &PROFILE_CHROME_142,
    &PROFILE_CHROME_143,
};

static const int profile_count = sizeof(profiles) / sizeof(profiles[0]);

/**
 * Get profile by name
 * Supports aliases:
 * - "chrome" -> "chrome143" (latest Chrome version)
 */
const browser_profile_t* browser_profile_get(const char *name) {
    if (!name) {
        return NULL;
    }

    /* Handle "chrome" alias -> latest Chrome (143) */
    const char *resolved_name = name;
    if (strcasecmp(name, "chrome") == 0) {
        resolved_name = "chrome143";
    }

    /* Find profile by name (case-insensitive) */
    for (int i = 0; i < profile_count; i++) {
        if (strcasecmp(profiles[i]->name, resolved_name) == 0) {
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
    /* For now, all browser types map to Chrome profiles */
    if (browser_type) {
        return browser_profile_get(browser_type);
    }
    return &PROFILE_CHROME_143;
}

/**
 * Get user agent for specific OS from profile
 */
const char* browser_profile_get_user_agent(const browser_profile_t *profile, os_type_t os) {
    if (!profile) {
        return NULL;
    }

    switch (os) {
        case OS_WINDOWS:
            return profile->user_agent_windows ? profile->user_agent_windows : profile->user_agent;
        case OS_LINUX:
            return profile->user_agent_linux ? profile->user_agent_linux : profile->user_agent;
        case OS_MACOS:
        default:
            return profile->user_agent;  /* macOS is the default */
    }
}

/**
 * List all available profiles
 */
const char** browser_profile_list(int *count) {
    if (count) {
        *count = profile_count;
    }

    static const char *names[64];  /* Increased to accommodate all profiles */
    for (int i = 0; i < profile_count && i < 64; i++) {
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

    /* Randomize GREASE values (Generate Random Extensions And Sustain Extensibility)
     * GREASE values should be different for each connection */
    if (variant->use_grease) {
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

    return variant;
}

/**
 * Destroy a generated profile
 */
void browser_profile_destroy(browser_profile_t *profile) {
    free(profile);
}
