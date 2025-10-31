/**
 * session.c - Session management
 */

#include "internal/session.h"
#include "internal/client.h"
#include "internal/cookies.h"
#include "internal/url.h"
#include "internal/tls.h"

#ifndef _WIN32
#include <pthread.h>
#else
#include <windows.h>
#endif

/* External SSL_CTX configuration mutex from client.c */
#ifndef _WIN32
extern pthread_mutex_t ssl_ctx_config_mutex;
#else
extern CRITICAL_SECTION ssl_ctx_config_mutex;
extern bool ssl_ctx_mutex_initialized;
#endif

/**
 * Create a new session
 */
httpmorph_session_t* httpmorph_session_create(httpmorph_browser_t browser_type) {
    httpmorph_session_t *session = calloc(1, sizeof(httpmorph_session_t));
    if (!session) {
        return NULL;
    }

    /* Create internal client */
    session->client = httpmorph_client_create();
    if (!session->client) {
        free(session);
        return NULL;
    }

    /* Select browser profile */
    const char *browser_str = "chrome";
    switch (browser_type) {
        case HTTPMORPH_BROWSER_CHROME: browser_str = "chrome"; break;
        case HTTPMORPH_BROWSER_FIREFOX: browser_str = "firefox"; break;
        case HTTPMORPH_BROWSER_SAFARI: browser_str = "safari"; break;
        case HTTPMORPH_BROWSER_EDGE: browser_str = "edge"; break;
        case HTTPMORPH_BROWSER_RANDOM:
            session->browser_profile = browser_profile_random();
            break;
        default: browser_str = "chrome"; break;
    }

    if (browser_type != HTTPMORPH_BROWSER_RANDOM) {
        session->browser_profile = browser_profile_by_type(browser_str);
    }

    if (session->browser_profile) {
        session->client->browser_profile = session->browser_profile;

        /* Protect SSL_CTX configuration with mutex */
#ifndef _WIN32
        pthread_mutex_lock(&ssl_ctx_config_mutex);
#else
        if (ssl_ctx_mutex_initialized) {
            EnterCriticalSection(&ssl_ctx_config_mutex);
        }
#endif

        httpmorph_configure_ssl_ctx(session->client->ssl_ctx, session->browser_profile);

#ifndef _WIN32
        pthread_mutex_unlock(&ssl_ctx_config_mutex);
#else
        if (ssl_ctx_mutex_initialized) {
            LeaveCriticalSection(&ssl_ctx_config_mutex);
        }
#endif
    }

    /* Initialize cookie jar */
    session->cookies = NULL;
    session->cookie_count = 0;

    /* Initialize connection pool for keep-alive */
    session->pool = pool_create();
    if (!session->pool) {
        httpmorph_session_destroy(session);
        return NULL;
    }

    return session;
}

/**
 * Destroy a session
 */
void httpmorph_session_destroy(httpmorph_session_t *session) {
    if (!session) {
        return;
    }

    /* Destroy connection pool (closes all pooled connections) */
    if (session->pool) {
        pool_destroy(session->pool);
    }

    if (session->client) {
        httpmorph_client_destroy(session->client);
    }

    /* Free cookies */
    cookie_t *cookie = session->cookies;
    while (cookie) {
        cookie_t *next = cookie->next;
        httpmorph_cookie_free(cookie);
        cookie = next;
    }

    free(session);
}

/**
 * Execute request with session (handles cookies automatically)
 */
httpmorph_response_t* httpmorph_session_request(httpmorph_session_t *session,
                                                const httpmorph_request_t *request) {
    if (!session || !request) {
        return NULL;
    }

    /* Parse URL to extract host for cookie domain */
    char *scheme = NULL, *host = NULL, *path = NULL;
    uint16_t port = 0;
    int parse_result = httpmorph_parse_url(request->url, &scheme, &host, &port, &path);
    if (parse_result != 0 || !host) {
        /* If we can't parse URL, still execute request but cookies won't work */
        host = strdup("unknown");
    }

    /* Add cookies from jar to request */
    char *cookie_header = httpmorph_get_cookies_for_request(session, host,
                                                   path ? path : "/",
                                                   request->use_tls);

    /* Create a mutable copy of the request to add cookie header */
    httpmorph_request_t *req_with_cookies = (httpmorph_request_t*)request;

    if (cookie_header) {
        /* Add Cookie header to request */
        httpmorph_request_add_header(req_with_cookies, "Cookie", cookie_header);
        free(cookie_header);
    }

    /* Execute the request with connection pooling */
    httpmorph_response_t *response = httpmorph_request_execute(session->client, req_with_cookies, session->pool);

    /* Parse Set-Cookie headers from response */
    if (response) {
        for (size_t i = 0; i < response->header_count; i++) {
            if (strcasecmp(response->headers[i].key, "Set-Cookie") == 0) {
                httpmorph_parse_set_cookie(session, response->headers[i].value, host);
            }
        }
    }

    /* Cleanup parsed URL components */
    free(scheme);
    free(host);
    free(path);

    return response;
}

/**
 * Get cookie count for session
 */
size_t httpmorph_session_cookie_count(httpmorph_session_t *session) {
    if (!session) {
        return 0;
    }
    return session->cookie_count;
}
