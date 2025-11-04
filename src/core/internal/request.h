/**
 * request.h - HTTP request structures and operations
 */

#ifndef REQUEST_H
#define REQUEST_H

#include "internal.h"

/**
 * Convert HTTP method enum to string
 *
 * @param method HTTP method
 * @return Method string
 */
const char* httpmorph_method_to_string(httpmorph_method_t method);

#endif /* REQUEST_H */
