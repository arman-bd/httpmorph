/**
 * buffer_pool.h - Buffer pooling for reduced allocation overhead
 *
 * Implements a slab allocator for common response buffer sizes.
 * Reuses buffers across requests to minimize malloc/free calls.
 */

#ifndef HTTPMORPH_BUFFER_POOL_H
#define HTTPMORPH_BUFFER_POOL_H

#include <stddef.h>
#include <stdbool.h>

/* Buffer size tiers (powers of 2 for efficient allocation) */
#define BUFFER_SIZE_4KB    4096
#define BUFFER_SIZE_16KB   16384
#define BUFFER_SIZE_64KB   65536
#define BUFFER_SIZE_256KB  262144

/* Number of buffers to keep per size tier */
#define BUFFERS_PER_TIER   8

/* Total number of size tiers */
#define NUM_TIERS          4

/**
 * Buffer pool structure
 * Thread-safe buffer allocator with size-based tiers
 */
typedef struct httpmorph_buffer_pool httpmorph_buffer_pool_t;

/**
 * Create a new buffer pool
 *
 * @return Initialized buffer pool or NULL on error
 */
httpmorph_buffer_pool_t* buffer_pool_create(void);

/**
 * Destroy a buffer pool and free all resources
 *
 * @param pool Buffer pool to destroy
 */
void buffer_pool_destroy(httpmorph_buffer_pool_t *pool);

/**
 * Allocate a buffer from the pool
 *
 * Returns a buffer of at least the requested size. May return a buffer
 * from the pool if one is available, otherwise allocates a new one.
 *
 * @param pool Buffer pool
 * @param size Minimum required size in bytes
 * @param actual_size Output parameter for actual buffer size (may be NULL)
 * @return Pointer to buffer or NULL on error
 */
void* buffer_pool_get(httpmorph_buffer_pool_t *pool, size_t size, size_t *actual_size);

/**
 * Return a buffer to the pool
 *
 * Returns the buffer to the pool for reuse. If the pool for this size
 * is full, the buffer is freed instead.
 *
 * @param pool Buffer pool
 * @param buffer Buffer to return
 * @param size Size of the buffer (must match size used in buffer_pool_get)
 */
void buffer_pool_put(httpmorph_buffer_pool_t *pool, void *buffer, size_t size);

/**
 * Get statistics about buffer pool usage
 *
 * @param pool Buffer pool
 * @param hits Output: number of successful pool retrievals
 * @param misses Output: number of new allocations
 * @param returns Output: number of buffers returned to pool
 */
void buffer_pool_stats(httpmorph_buffer_pool_t *pool,
                       size_t *hits, size_t *misses, size_t *returns);

#endif /* HTTPMORPH_BUFFER_POOL_H */
