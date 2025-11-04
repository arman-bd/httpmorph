/**
 * url.c - URL parsing and manipulation
 */

#include "internal/url.h"

/**
 * Parse a URL into its components
 */
int httpmorph_parse_url(const char *url, char **scheme, char **host,
                         uint16_t *port, char **path) {
    if (!url || !scheme || !host || !port || !path) {
        return -1;
    }

    /* Simple URL parser */
    const char *p = url;

    /* Parse scheme */
    const char *scheme_end = strstr(p, "://");
    if (!scheme_end) {
        return -1;
    }

    *scheme = strndup(p, scheme_end - p);
    p = scheme_end + 3;

    /* Parse host and optional port */
    const char *path_start = strchr(p, '/');
    const char *port_start = strchr(p, ':');

    if (port_start && (!path_start || port_start < path_start)) {
        /* Port specified */
        *host = strndup(p, port_start - p);
        p = port_start + 1;

        char *end;
        long port_num = strtol(p, &end, 10);
        if (port_num <= 0 || port_num > 65535) {
            free(*host);
            free(*scheme);
            return -1;
        }
        *port = (uint16_t)port_num;
        p = end;
    } else {
        /* Default port */
        *port = (strcmp(*scheme, "https") == 0) ? 443 : 80;

        if (path_start) {
            *host = strndup(p, path_start - p);
            p = path_start;
        } else {
            *host = strdup(p);
            p = p + strlen(p);
        }
    }

    /* Parse path */
    if (*p == '\0') {
        *path = strdup("/");
    } else {
        *path = strdup(p);
    }

    return 0;
}
