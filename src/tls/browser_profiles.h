/**
 * browser_profiles.h - Browser TLS/HTTP fingerprint profiles
 *
 * Contains detailed profiles for popular browsers to enable accurate impersonation
 */

#ifndef BROWSER_PROFILES_H
#define BROWSER_PROFILES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum sizes */
#define MAX_CIPHER_SUITES 32
#define MAX_EXTENSIONS 24
#define MAX_CURVES 16
#define MAX_SIG_ALGORITHMS 24
#define MAX_ALPN_PROTOCOLS 8
#define MAX_HTTP2_SETTINGS 16

/* TLS version */
typedef enum {
    TLS_VERSION_1_0 = 0x0301,
    TLS_VERSION_1_1 = 0x0302,
    TLS_VERSION_1_2 = 0x0303,
    TLS_VERSION_1_3 = 0x0304,
} tls_version_t;

/* TLS extension types */
typedef enum {
    TLS_EXT_SERVER_NAME = 0,
    TLS_EXT_STATUS_REQUEST = 5,
    TLS_EXT_SUPPORTED_GROUPS = 10,
    TLS_EXT_EC_POINT_FORMATS = 11,
    TLS_EXT_SIGNATURE_ALGORITHMS = 13,
    TLS_EXT_ALPN = 16,
    TLS_EXT_SIGNED_CERTIFICATE_TIMESTAMP = 18,
    TLS_EXT_PADDING = 21,
    TLS_EXT_EXTENDED_MASTER_SECRET = 23,
    TLS_EXT_SESSION_TICKET = 35,
    TLS_EXT_SUPPORTED_VERSIONS = 43,
    TLS_EXT_PSK_KEY_EXCHANGE_MODES = 45,
    TLS_EXT_KEY_SHARE = 51,
    TLS_EXT_COMPRESS_CERTIFICATE = 27,
    TLS_EXT_APPLICATION_SETTINGS = 17513,
    TLS_EXT_GREASE = 0x0a0a,  /* GREASE values vary */
} tls_extension_t;

/* Browser TLS profile */
typedef struct {
    const char *name;           /* e.g., "Chrome 131" */
    const char *version;        /* e.g., "131.0.6778.109" */
    const char *user_agent;     /* Full user agent string */

    /* TLS configuration */
    tls_version_t min_tls_version;
    tls_version_t max_tls_version;

    /* Cipher suites in exact order */
    uint16_t cipher_suites[MAX_CIPHER_SUITES];
    int cipher_suite_count;

    /* Extensions in exact order */
    uint16_t extensions[MAX_EXTENSIONS];
    int extension_count;

    /* Supported curves/groups */
    uint16_t curves[MAX_CURVES];
    int curve_count;

    /* Signature algorithms */
    uint16_t signature_algorithms[MAX_SIG_ALGORITHMS];
    int signature_algorithm_count;

    /* ALPN protocols in order */
    const char *alpn_protocols[MAX_ALPN_PROTOCOLS];
    int alpn_protocol_count;

    /* GREASE configuration */
    bool use_grease;
    uint16_t grease_cipher;
    uint16_t grease_extension;
    uint16_t grease_group;

    /* HTTP/2 fingerprint */
    struct {
        uint32_t settings[MAX_HTTP2_SETTINGS][2];  /* [id, value] pairs */
        int setting_count;
        uint32_t window_update;
        uint8_t priority_frames[16];  /* Stream priority configuration */
        int priority_frame_count;
    } http2;

    /* JA3 fingerprint (precomputed) */
    char ja3_hash[33];  /* MD5 hash as hex string */

} browser_profile_t;

/* Browser profile database */

/**
 * Get profile by name
 */
const browser_profile_t* browser_profile_get(const char *name);

/**
 * Get random profile
 */
const browser_profile_t* browser_profile_random(void);

/**
 * Get profile by browser type
 */
const browser_profile_t* browser_profile_by_type(const char *browser_type);

/**
 * List all available profiles
 */
const char** browser_profile_list(int *count);

/**
 * Generate dynamic profile based on real browser with variations
 */
browser_profile_t* browser_profile_generate_variant(const browser_profile_t *base);

/**
 * Destroy a generated profile
 */
void browser_profile_destroy(browser_profile_t *profile);

/* Predefined profiles */
extern const browser_profile_t PROFILE_CHROME_131;
extern const browser_profile_t PROFILE_CHROME_124;
extern const browser_profile_t PROFILE_FIREFOX_122;
extern const browser_profile_t PROFILE_SAFARI_17;
extern const browser_profile_t PROFILE_EDGE_122;

#ifdef __cplusplus
}
#endif

#endif /* BROWSER_PROFILES_H */
