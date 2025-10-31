/**
 * cookies.h - Cookie management
 */

#ifndef COOKIES_H
#define COOKIES_H

#include "internal.h"

/**
 * Free a cookie structure
 */
void httpmorph_cookie_free(cookie_t *cookie);

/**
 * Parse Set-Cookie header and add to session
 *
 * @param session Session to add cookie to
 * @param header_value Set-Cookie header value
 * @param request_domain Domain of the request that received this cookie
 */
void httpmorph_parse_set_cookie(httpmorph_session_t *session,
                                 const char *header_value,
                                 const char *request_domain);

/**
 * Get cookies for a request as a Cookie header value
 *
 * @param session Session containing cookies
 * @param domain Request domain
 * @param path Request path
 * @param is_secure Whether request uses HTTPS
 * @return Cookie header value (caller must free) or NULL if no cookies
 */
char* httpmorph_get_cookies_for_request(httpmorph_session_t *session,
                                         const char *domain,
                                         const char *path,
                                         bool is_secure);

#endif /* COOKIES_H */
