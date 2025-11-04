/**
 * util.h - Utility functions
 */

#ifndef UTIL_H
#define UTIL_H

#include "internal.h"

/**
 * Get current time in microseconds
 */
uint64_t httpmorph_get_time_us(void);

/**
 * Base64 encode a binary buffer
 * Returns newly allocated string that must be freed by caller
 */
char* httpmorph_base64_encode(const char *input, size_t length);

#endif /* UTIL_H */
