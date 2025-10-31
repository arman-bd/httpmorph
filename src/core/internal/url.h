/**
 * url.h - URL parsing and manipulation
 */

#ifndef URL_H
#define URL_H

#include "internal.h"

/**
 * Parse a URL into its components
 *
 * @param url Full URL string
 * @param scheme Output: scheme (http, https) - caller must free
 * @param host Output: hostname - caller must free
 * @param port Output: port number
 * @param path Output: path including query string - caller must free
 * @return 0 on success, -1 on error
 */
int httpmorph_parse_url(const char *url, char **scheme, char **host,
                         uint16_t *port, char **path);

#endif /* URL_H */
