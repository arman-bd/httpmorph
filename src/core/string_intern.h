/**
 * string_intern.h - String interning for common HTTP headers
 *
 * Reduces memory usage by deduplicating common header strings.
 * Common headers are stored once and reused across all requests/responses.
 */

#ifndef HTTPMORPH_STRING_INTERN_H
#define HTTPMORPH_STRING_INTERN_H

#include <stddef.h>

/**
 * Get an interned string for a common header name
 * Returns the interned string if found, NULL otherwise
 *
 * @param str String to intern
 * @param len Length of string
 * @return Interned string pointer or NULL if not a common header
 */
const char* string_intern_get(const char *str, size_t len);

/**
 * Check if a string is interned (for optimization)
 *
 * @param str String to check
 * @return 1 if interned, 0 otherwise
 */
int string_intern_is_interned(const char *str);

#endif /* HTTPMORPH_STRING_INTERN_H */
