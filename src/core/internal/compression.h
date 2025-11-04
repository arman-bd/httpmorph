/**
 * compression.h - Content decompression
 */

#ifndef COMPRESSION_H
#define COMPRESSION_H

#include "internal.h"

/**
 * Decompress gzip-compressed response body
 *
 * @param response Response with compressed body
 * @return 0 on success, -1 on error
 */
int httpmorph_decompress_gzip(httpmorph_response_t *response);

/**
 * Decompress deflate-compressed response body
 *
 * @param response Response with compressed body
 * @return 0 on success, -1 on error
 */
int httpmorph_decompress_deflate(httpmorph_response_t *response);

/**
 * Automatically decompress response body based on Content-Encoding header
 * Supports gzip, deflate, and identity encodings
 *
 * @param response Response to decompress
 * @return 0 on success, -1 on error
 */
int httpmorph_auto_decompress(httpmorph_response_t *response);

#endif /* COMPRESSION_H */
