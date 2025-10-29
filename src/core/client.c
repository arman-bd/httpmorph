/**
 * client.c - HTTP client management
 */

#include "internal/client.h"
#include "internal/tls.h"
#include "internal/network.h"
#include "buffer_pool.h"

#ifndef _WIN32
#include <pthread.h>
#else
#include <windows.h>
#endif

/* Library state - protected by pthread_once for thread-safe initialization */
#ifndef _WIN32
static pthread_once_t init_once_control = PTHREAD_ONCE_INIT;
#else
static INIT_ONCE init_once_control = INIT_ONCE_STATIC_INIT;
#endif

static bool httpmorph_init_status = false;
static io_engine_t *default_io_engine = NULL;

/* Mutex for SSL_CTX configuration (BoringSSL SSL_CTX_* functions are not thread-safe) */
/* Non-static so it can be accessed from session.c via extern declaration */
#ifndef _WIN32
pthread_mutex_t ssl_ctx_config_mutex = PTHREAD_MUTEX_INITIALIZER;
#else
CRITICAL_SECTION ssl_ctx_config_mutex;
bool ssl_ctx_mutex_initialized = false;
#endif

/**
 * Internal initialization function (called once via pthread_once)
 */
#ifndef _WIN32
static void httpmorph_init_internal(void) {
#else
static BOOL CALLBACK httpmorph_init_internal(PINIT_ONCE InitOnce, PVOID Parameter, PVOID *Context) {
#endif

#ifdef _WIN32
    /* Initialize Winsock on Windows */
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", result);
        httpmorph_init_status = false;
        return FALSE;
    }

    /* Initialize SSL_CTX config mutex */
    InitializeCriticalSection(&ssl_ctx_config_mutex);
    ssl_ctx_mutex_initialized = true;
#endif

    /* Initialize BoringSSL - no explicit initialization needed */
    /* BoringSSL initializes automatically, unlike old OpenSSL versions */

    /* Create default I/O engine */
    default_io_engine = io_engine_create(256);
    if (!default_io_engine) {
#ifdef _WIN32
        WSACleanup();
        httpmorph_init_status = false;
        return FALSE;
#else
        httpmorph_init_status = false;
        return;
#endif
    }

    httpmorph_init_status = true;
#ifdef _WIN32
    return TRUE;
#endif
}

/**
 * Initialize the httpmorph library (thread-safe)
 */
int httpmorph_init(void) {
#ifndef _WIN32
    /* Use pthread_once to ensure initialization happens exactly once */
    pthread_once(&init_once_control, httpmorph_init_internal);
#else
    /* Windows equivalent: InitOnceExecuteOnce */
    InitOnceExecuteOnce(&init_once_control, httpmorph_init_internal, NULL, NULL);
#endif

    return httpmorph_init_status ? HTTPMORPH_OK : HTTPMORPH_ERROR_MEMORY;
}

/**
 * Cleanup the httpmorph library (thread-safe)
 */
void httpmorph_cleanup(void) {
    /* Thread-safe cleanup: use a static flag with atomic-like access */
    static bool cleanup_in_progress = false;
    static bool cleanup_done = false;

    /* Quick check without lock - if already done, return immediately */
    if (cleanup_done || !httpmorph_init_status) {
        return;
    }

#ifndef _WIN32
    /* Use SSL config mutex to serialize cleanup calls */
    pthread_mutex_lock(&ssl_ctx_config_mutex);
#else
    if (ssl_ctx_mutex_initialized) {
        EnterCriticalSection(&ssl_ctx_config_mutex);
    }
#endif

    /* Check again after acquiring lock */
    if (cleanup_done || cleanup_in_progress) {
#ifndef _WIN32
        pthread_mutex_unlock(&ssl_ctx_config_mutex);
#else
        if (ssl_ctx_mutex_initialized) {
            LeaveCriticalSection(&ssl_ctx_config_mutex);
        }
#endif
        return;
    }

    cleanup_in_progress = true;

    /* Clear DNS cache first */
    dns_cache_clear();

    /* Destroy I/O engine last to ensure no pending operations */
    if (default_io_engine) {
        io_engine_destroy(default_io_engine);
        default_io_engine = NULL;
    }

    /* BoringSSL cleanup - these functions may not exist or be needed */
    /* Modern BoringSSL doesn't require explicit cleanup */
    /* ERR_free_strings() and EVP_cleanup() are legacy OpenSSL */

    httpmorph_init_status = false;
    cleanup_done = true;
    cleanup_in_progress = false;

#ifndef _WIN32
    pthread_mutex_unlock(&ssl_ctx_config_mutex);
#else
    if (ssl_ctx_mutex_initialized) {
        LeaveCriticalSection(&ssl_ctx_config_mutex);
    }

    /* Cleanup SSL_CTX config mutex last */
    if (ssl_ctx_mutex_initialized) {
        DeleteCriticalSection(&ssl_ctx_config_mutex);
        ssl_ctx_mutex_initialized = false;
    }

    /* Cleanup Winsock on Windows */
    WSACleanup();
#endif
}

/**
 * Get library version string
 */
const char* httpmorph_version(void) {
    static char version[32];
    snprintf(version, sizeof(version), "%d.%d.%d",
             HTTPMORPH_VERSION_MAJOR,
             HTTPMORPH_VERSION_MINOR,
             HTTPMORPH_VERSION_PATCH);
    return version;
}

/**
 * Create a new HTTP client
 */
httpmorph_client_t* httpmorph_client_create(void) {
    /* Thread-safe initialization using pthread_once */
    httpmorph_init();

    httpmorph_client_t *client = calloc(1, sizeof(httpmorph_client_t));
    if (!client) {
        return NULL;
    }

    /* Create SSL context */
    client->ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (!client->ssl_ctx) {
        free(client);
        return NULL;
    }

    /* Disable SSL session caching to avoid BoringSSL state issues */
    SSL_CTX_set_session_cache_mode(client->ssl_ctx, SSL_SESS_CACHE_OFF);

    /* Set session timeout (5 minutes = 300 seconds) - still set even if caching is off */
    SSL_CTX_set_timeout(client->ssl_ctx, 300);

    /* Enable session ticket support for TLS 1.3 and better TLS 1.2 resumption */
    #ifndef SSL_OP_NO_TICKET
    /* BoringSSL doesn't have SSL_OP_NO_TICKET, session tickets are enabled by default */
    #else
    /* For other SSL libraries, ensure tickets are NOT disabled */
    SSL_CTX_clear_options(client->ssl_ctx, SSL_OP_NO_TICKET);
    #endif

    /* Default configuration */
    client->timeout_ms = 30000;  /* 30 seconds */
    client->follow_redirects = false;  /* Python layer handles redirects for better control */
    client->max_redirects = 10;
    client->io_engine = default_io_engine;

    /* Default to Chrome browser profile - protected by mutex since SSL_CTX_* functions are not thread-safe */
    client->browser_profile = &PROFILE_CHROME_131;

#ifndef _WIN32
    pthread_mutex_lock(&ssl_ctx_config_mutex);
#else
    if (ssl_ctx_mutex_initialized) {
        EnterCriticalSection(&ssl_ctx_config_mutex);
    }
#endif

    httpmorph_configure_ssl_ctx(client->ssl_ctx, client->browser_profile);

#ifndef _WIN32
    pthread_mutex_unlock(&ssl_ctx_config_mutex);
#else
    if (ssl_ctx_mutex_initialized) {
        LeaveCriticalSection(&ssl_ctx_config_mutex);
    }
#endif

    /* Create buffer pool for response bodies */
    client->buffer_pool = buffer_pool_create();
    if (!client->buffer_pool) {
        SSL_CTX_free(client->ssl_ctx);
        free(client);
        return NULL;
    }

    return client;
}

/**
 * Get the connection pool from a client
 */
httpmorph_pool_t* httpmorph_client_get_pool(httpmorph_client_t *client) {
    if (!client) {
        return NULL;
    }
    return client->pool;
}

/**
 * Destroy an HTTP client
 */
void httpmorph_client_destroy(httpmorph_client_t *client) {
    if (!client) {
        return;
    }

    if (client->ssl_ctx) {
        SSL_CTX_free(client->ssl_ctx);
    }

    if (client->buffer_pool) {
        buffer_pool_destroy(client->buffer_pool);
    }

    free(client);
}
