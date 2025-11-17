/**
 * request_builder.c - Fast request building implementation
 */

#include "request_builder.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Initial capacity if not specified */
#define DEFAULT_CAPACITY 512

/* Growth factor for reallocation */
#define GROWTH_FACTOR 2

/**
 * Ensure builder has at least `needed` bytes of free space
 */
static int ensure_capacity(request_builder_t *builder, size_t needed) {
    if (builder->len + needed <= builder->capacity) {
        return 0;  /* Enough space */
    }

    /* Calculate new capacity with overflow protection */
    size_t new_capacity = builder->capacity;
    size_t target = builder->len + needed;

    /* Check for overflow in target calculation */
    if (target < builder->len) {
        return -1;  /* Overflow detected */
    }

    while (new_capacity < target) {
        /* Check for overflow before multiplication */
        if (new_capacity > SIZE_MAX / GROWTH_FACTOR) {
            return -1;  /* Would overflow */
        }
        new_capacity *= GROWTH_FACTOR;
    }

    /* Reallocate */
    char *new_data = (char*)realloc(builder->data, new_capacity);
    if (!new_data) {
        return -1;
    }

    builder->data = new_data;
    builder->capacity = new_capacity;
    return 0;
}

/**
 * Create a new request builder
 */
request_builder_t* request_builder_create(size_t initial_capacity) {
    request_builder_t *builder = (request_builder_t*)malloc(sizeof(request_builder_t));
    if (!builder) {
        return NULL;
    }

    if (initial_capacity == 0) {
        initial_capacity = DEFAULT_CAPACITY;
    }

    builder->data = (char*)malloc(initial_capacity);
    if (!builder->data) {
        free(builder);
        return NULL;
    }

    builder->len = 0;
    builder->capacity = initial_capacity;
    return builder;
}

/**
 * Destroy a request builder
 */
void request_builder_destroy(request_builder_t *builder) {
    if (!builder) {
        return;
    }
    free(builder->data);
    free(builder);
}

/**
 * Append a string with known length (fast memcpy)
 */
int request_builder_append(request_builder_t *builder, const char *str, size_t len) {
    if (!builder || !str || len == 0) {
        return 0;
    }

    if (ensure_capacity(builder, len) != 0) {
        return -1;
    }

    memcpy(builder->data + builder->len, str, len);
    builder->len += len;
    return 0;
}

/**
 * Append a null-terminated string
 */
int request_builder_append_str(request_builder_t *builder, const char *str) {
    if (!str) {
        return 0;
    }
    return request_builder_append(builder, str, strlen(str));
}

/**
 * Append an unsigned integer as decimal string
 */
int request_builder_append_uint(request_builder_t *builder, uint64_t value) {
    char buf[32];  /* Enough for uint64_t */
    int len = snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
    if (len < 0) {
        return -1;
    }
    return request_builder_append(builder, buf, len);
}

/**
 * Append a formatted header line
 */
int request_builder_append_header(request_builder_t *builder,
                                   const char *key, size_t key_len,
                                   const char *value, size_t value_len) {
    /* Total: key_len + 2 (": ") + value_len + 2 ("\r\n") */
    size_t total = key_len + value_len + 4;

    if (ensure_capacity(builder, total) != 0) {
        return -1;
    }

    char *p = builder->data + builder->len;

    /* Copy key */
    memcpy(p, key, key_len);
    p += key_len;

    /* Add ": " */
    *p++ = ':';
    *p++ = ' ';

    /* Copy value */
    memcpy(p, value, value_len);
    p += value_len;

    /* Add "\r\n" */
    *p++ = '\r';
    *p++ = '\n';

    builder->len += total;
    return 0;
}

/**
 * Get the current buffer data
 */
const char* request_builder_data(const request_builder_t *builder, size_t *len) {
    if (!builder) {
        if (len) *len = 0;
        return NULL;
    }

    if (len) {
        *len = builder->len;
    }
    return builder->data;
}
