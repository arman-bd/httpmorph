/**
 * response.h - HTTP response structures and operations
 */

#ifndef RESPONSE_H
#define RESPONSE_H

#include "internal.h"

/**
 * Create a new response structure
 *
 * @param buffer_pool Buffer pool for body allocation (can be NULL for no pooling)
 * @return Newly allocated response or NULL on error
 */
httpmorph_response_t* httpmorph_response_create(httpmorph_buffer_pool_t *buffer_pool);

/**
 * Parse HTTP response status line
 *
 * @param line Status line (e.g., "HTTP/1.1 200 OK")
 * @param response Response to populate
 * @return 0 on success, -1 on error
 */
int httpmorph_parse_response_line(const char *line, httpmorph_response_t *response);

/**
 * Add a header to response (with length for HTTP/2)
 *
 * @param response Response to add header to
 * @param name Header name
 * @param namelen Length of header name
 * @param value Header value
 * @param valuelen Length of header value
 * @return 0 on success, -1 on error
 */
int httpmorph_response_add_header_internal(httpmorph_response_t *response,
                                            const char *name, size_t namelen,
                                            const char *value, size_t valuelen);

#endif /* RESPONSE_H */
