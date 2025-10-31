/**
 * request_builder.h - Fast request building utilities
 *
 * Optimized buffer building using direct memory operations
 * instead of snprintf for better performance.
 */

#ifndef HTTPMORPH_REQUEST_BUILDER_H
#define HTTPMORPH_REQUEST_BUILDER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * Dynamic buffer for building HTTP requests
 */
typedef struct {
    char *data;
    size_t len;
    size_t capacity;
} request_builder_t;

/**
 * Create a new request builder with initial capacity
 *
 * @param initial_capacity Initial buffer size
 * @return New request builder or NULL on error
 */
request_builder_t* request_builder_create(size_t initial_capacity);

/**
 * Destroy a request builder and free resources
 *
 * @param builder Builder to destroy
 */
void request_builder_destroy(request_builder_t *builder);

/**
 * Append a string to the builder (fast memcpy)
 *
 * @param builder Builder to append to
 * @param str String to append
 * @param len Length of string
 * @return 0 on success, -1 on error
 */
int request_builder_append(request_builder_t *builder, const char *str, size_t len);

/**
 * Append a null-terminated string (calculates length)
 *
 * @param builder Builder to append to
 * @param str Null-terminated string to append
 * @return 0 on success, -1 on error
 */
int request_builder_append_str(request_builder_t *builder, const char *str);

/**
 * Append an unsigned integer as decimal string
 *
 * @param builder Builder to append to
 * @param value Value to append
 * @return 0 on success, -1 on error
 */
int request_builder_append_uint(request_builder_t *builder, uint64_t value);

/**
 * Append a formatted header line: "Key: Value\r\n"
 *
 * @param builder Builder to append to
 * @param key Header key
 * @param key_len Length of key
 * @param value Header value
 * @param value_len Length of value
 * @return 0 on success, -1 on error
 */
int request_builder_append_header(request_builder_t *builder,
                                   const char *key, size_t key_len,
                                   const char *value, size_t value_len);

/**
 * Get the current buffer data
 *
 * @param builder Builder to get data from
 * @param len Output: length of data
 * @return Pointer to buffer data (valid until next append or destroy)
 */
const char* request_builder_data(const request_builder_t *builder, size_t *len);

#endif /* HTTPMORPH_REQUEST_BUILDER_H */
