/**
 * compression.c - Content decompression
 */

#include "internal/compression.h"
#include "buffer_pool.h"
#include <zlib.h>

/**
 * Internal helper to decompress data using zlib
 */
static int decompress_zlib(httpmorph_response_t *response, int windowBits) {
    if (!response || !response->body || response->body_len == 0) {
        return -1;
    }

    /* Allocate decompressed buffer (assume 10x compression ratio) */
    size_t decompressed_capacity = response->body_len * 10;
    if (decompressed_capacity < 16384) decompressed_capacity = 16384;

    /* Get buffer from pool if available, otherwise malloc */
    uint8_t *decompressed = NULL;
    size_t decompressed_actual_size = 0;
    if (response->_buffer_pool) {
        decompressed = buffer_pool_get((httpmorph_buffer_pool_t*)response->_buffer_pool,
                                      decompressed_capacity, &decompressed_actual_size);
    } else {
        decompressed = malloc(decompressed_capacity);
        decompressed_actual_size = decompressed_capacity;
    }

    if (!decompressed) {
        return -1;
    }

    /* Initialize zlib */
    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    stream.next_in = response->body;
    stream.avail_in = (uInt)response->body_len;
    stream.next_out = decompressed;
    stream.avail_out = (uInt)decompressed_capacity;

    int ret = inflateInit2(&stream, windowBits);
    if (ret != Z_OK) {
        free(decompressed);
        return -1;
    }

    /* Decompress */
    ret = inflate(&stream, Z_FINISH);

    /* Handle need for more output space */
    while (ret == Z_BUF_ERROR || ret == Z_OK) {
        size_t new_capacity = decompressed_capacity * 2;
        size_t new_actual_size = 0;
        uint8_t *new_decompressed = NULL;

        /* Get new buffer from pool if available */
        if (response->_buffer_pool) {
            new_decompressed = buffer_pool_get((httpmorph_buffer_pool_t*)response->_buffer_pool,
                                              new_capacity, &new_actual_size);
            if (new_decompressed) {
                /* Copy existing data to new buffer */
                memcpy(new_decompressed, decompressed, decompressed_capacity);
                /* Return old buffer to pool */
                buffer_pool_put((httpmorph_buffer_pool_t*)response->_buffer_pool,
                              decompressed, decompressed_actual_size);
            }
        } else {
            new_decompressed = realloc(decompressed, new_capacity);
            new_actual_size = new_capacity;
        }

        if (!new_decompressed) {
            inflateEnd(&stream);
            /* Return buffer to pool or free */
            if (response->_buffer_pool) {
                buffer_pool_put((httpmorph_buffer_pool_t*)response->_buffer_pool,
                              decompressed, decompressed_actual_size);
            } else {
                free(decompressed);
            }
            return -1;
        }

        decompressed = new_decompressed;
        decompressed_actual_size = new_actual_size;
        stream.next_out = decompressed + decompressed_capacity;
        stream.avail_out = (uInt)(new_capacity - decompressed_capacity);
        decompressed_capacity = new_capacity;

        ret = inflate(&stream, Z_FINISH);
    }

    if (ret != Z_STREAM_END) {
        inflateEnd(&stream);
        /* Return buffer to pool or free */
        if (response->_buffer_pool) {
            buffer_pool_put((httpmorph_buffer_pool_t*)response->_buffer_pool,
                          decompressed, decompressed_actual_size);
        } else {
            free(decompressed);
        }
        return -1;
    }

    size_t decompressed_size = stream.total_out;
    inflateEnd(&stream);

    /* Replace compressed body with decompressed */
    /* Return old buffer to pool if available, otherwise free */
    if (response->body) {
        if (response->_buffer_pool) {
            buffer_pool_put((httpmorph_buffer_pool_t*)response->_buffer_pool,
                          response->body, response->_body_actual_size);
        } else {
            free(response->body);
        }
    }

    response->body = decompressed;
    response->body_len = decompressed_size;
    response->body_capacity = decompressed_capacity;
    response->_body_actual_size = decompressed_actual_size;  /* Update actual size (from pool or malloc) */

    return 0;
}

/**
 * Decompress gzip-compressed response body
 */
int httpmorph_decompress_gzip(httpmorph_response_t *response) {
    if (!response || !response->body || response->body_len == 0) {
        return -1;
    }

    /* Check if body is gzip-compressed (starts with 0x1f 0x8b) */
    if (response->body_len < 2 ||
        response->body[0] != 0x1f ||
        response->body[1] != 0x8b) {
        return 0;  /* Not gzipped, nothing to do */
    }

    /* Use inflateInit2 with windowBits=15+16 for gzip */
    return decompress_zlib(response, 15 + 16);
}

/**
 * Decompress deflate-compressed response body
 */
int httpmorph_decompress_deflate(httpmorph_response_t *response) {
    if (!response || !response->body || response->body_len == 0) {
        return -1;
    }

    /* Try raw deflate first (windowBits=-15) */
    int ret = decompress_zlib(response, -15);
    if (ret != 0) {
        /* Try zlib-wrapped deflate (windowBits=15) */
        ret = decompress_zlib(response, 15);
    }

    return ret;
}

/**
 * Automatically decompress response body based on Content-Encoding header
 */
int httpmorph_auto_decompress(httpmorph_response_t *response) {
    if (!response) {
        return -1;
    }

    /* Get Content-Encoding header */
    const char *encoding = httpmorph_response_get_header(response, "Content-Encoding");
    if (!encoding) {
        return 0;  /* No encoding, nothing to do */
    }

    /* Handle gzip */
    if (strcasecmp(encoding, "gzip") == 0) {
        return httpmorph_decompress_gzip(response);
    }

    /* Handle deflate */
    if (strcasecmp(encoding, "deflate") == 0) {
        return httpmorph_decompress_deflate(response);
    }

    /* Handle identity (no compression) */
    if (strcasecmp(encoding, "identity") == 0) {
        return 0;
    }

    /* Unknown encoding - leave as-is */
    return 0;
}
