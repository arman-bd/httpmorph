/**
 * cookies.c - Cookie management
 */

#include "internal/cookies.h"

/**
 * Free a cookie structure
 */
void httpmorph_cookie_free(cookie_t *cookie) {
    if (!cookie) return;
    free(cookie->name);
    free(cookie->value);
    free(cookie->domain);
    free(cookie->path);
    free(cookie);
}

/**
 * Parse Set-Cookie header and add to session
 */
void httpmorph_parse_set_cookie(httpmorph_session_t *session,
                                 const char *header_value,
                                 const char *request_domain) {
    if (!session || !header_value || !request_domain) {
        return;
    }

    cookie_t *cookie = calloc(1, sizeof(cookie_t));
    if (!cookie) return;

    /* Parse cookie: name=value; attributes */
    char *header_copy = strdup(header_value);
    if (!header_copy) {
        free(cookie);
        return;
    }

    /* Extract name=value */
    char *semicolon = strchr(header_copy, ';');
    if (semicolon) *semicolon = '\0';

    char *equals = strchr(header_copy, '=');
    if (equals) {
        *equals = '\0';
        cookie->name = strdup(header_copy);
        cookie->value = strdup(equals + 1);
    } else {
        free(header_copy);
        free(cookie);
        return;
    }

    /* Set default domain and path */
    cookie->domain = strdup(request_domain);
    cookie->path = strdup("/");
    cookie->expires = 0;  /* Session cookie by default */
    cookie->secure = false;
    cookie->http_only = false;

    /* Parse attributes if present */
    if (semicolon) {
        char *attr = semicolon + 1;
        while (attr && *attr) {
            /* Skip whitespace */
            while (*attr == ' ') attr++;

            if (strncasecmp(attr, "Domain=", 7) == 0) {
                attr += 7;
                char *end = strchr(attr, ';');
                size_t len = end ? (size_t)(end - attr) : strlen(attr);
                free(cookie->domain);
                cookie->domain = strndup(attr, len);
                attr = end;
            } else if (strncasecmp(attr, "Path=", 5) == 0) {
                attr += 5;
                char *end = strchr(attr, ';');
                size_t len = end ? (size_t)(end - attr) : strlen(attr);
                free(cookie->path);
                cookie->path = strndup(attr, len);
                attr = end;
            } else if (strncasecmp(attr, "Secure", 6) == 0) {
                cookie->secure = true;
                attr = strchr(attr, ';');
            } else if (strncasecmp(attr, "HttpOnly", 8) == 0) {
                cookie->http_only = true;
                attr = strchr(attr, ';');
            } else {
                /* Skip unknown attribute */
                attr = strchr(attr, ';');
            }

            if (attr) attr++;
        }
    }

    free(header_copy);

    /* Add to session's cookie list */
    cookie->next = session->cookies;
    session->cookies = cookie;
    session->cookie_count++;
}

/**
 * Get cookies for a request as a Cookie header value
 */
char* httpmorph_get_cookies_for_request(httpmorph_session_t *session,
                                         const char *domain,
                                         const char *path,
                                         bool is_secure) {
    if (!session || !domain || !path) return NULL;
    if (session->cookie_count == 0) return NULL;

    /* Build cookie header value with bounds checking */
    const size_t buffer_size = 4096;
    char *cookie_header = malloc(buffer_size);
    if (!cookie_header) return NULL;

    cookie_header[0] = '\0';
    size_t used = 0;
    bool first = true;

    cookie_t *cookie = session->cookies;
    while (cookie) {
        /* Check if cookie matches request */
        bool domain_match = (strcasecmp(cookie->domain, domain) == 0 ||
                            (cookie->domain[0] == '.' && strstr(domain, cookie->domain + 1) != NULL));
        bool path_match = (strncmp(cookie->path, path, strlen(cookie->path)) == 0);
        bool secure_match = (!cookie->secure || is_secure);

        if (domain_match && path_match && secure_match) {
            /* Calculate space needed: "; " + name + "=" + value */
            size_t needed = strlen(cookie->name) + 1 + strlen(cookie->value);
            if (!first) needed += 2;  /* "; " prefix */

            /* Check if we have space (leave room for null terminator) */
            if (used + needed >= buffer_size - 1) {
                /* Buffer would overflow - stop adding cookies */
                break;
            }

            /* Safe concatenation with bounds checking */
            if (!first) {
                used += snprintf(cookie_header + used, buffer_size - used, "; ");
            }
            used += snprintf(cookie_header + used, buffer_size - used, "%s=%s",
                           cookie->name, cookie->value);
            first = false;
        }

        cookie = cookie->next;
    }

    if (cookie_header[0] == '\0') {
        free(cookie_header);
        return NULL;
    }

    return cookie_header;
}
