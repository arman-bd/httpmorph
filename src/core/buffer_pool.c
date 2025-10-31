/**
 * buffer_pool.c - Buffer pooling implementation
 *
 * Slab allocator for common response buffer sizes.
 */

#include "buffer_pool.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <pthread.h>
#endif

/* Buffer tier structure */
typedef struct buffer_tier {
    size_t buffer_size;           /* Size of buffers in this tier */
    void *buffers[BUFFERS_PER_TIER];  /* Array of available buffers */
    int available_count;          /* Number of available buffers */
} buffer_tier_t;

/* Buffer pool structure */
struct httpmorph_buffer_pool {
    buffer_tier_t tiers[NUM_TIERS];  /* 4KB, 16KB, 64KB, 256KB */

    /* Statistics */
    size_t hits;       /* Pool retrievals */
    size_t misses;     /* New allocations */
    size_t returns;    /* Buffers returned */

    /* Thread safety */
#ifdef _WIN32
    CRITICAL_SECTION mutex;
#else
    pthread_mutex_t mutex;
#endif
};

/**
 * Get tier index for a given size
 * Returns the smallest tier that can fit the requested size
 */
static int get_tier_index(size_t size) {
    if (size <= BUFFER_SIZE_4KB) return 0;
    if (size <= BUFFER_SIZE_16KB) return 1;
    if (size <= BUFFER_SIZE_64KB) return 2;
    if (size <= BUFFER_SIZE_256KB) return 3;
    return -1;  /* Too large for pooling */
}

/**
 * Get buffer size for a tier index
 */
static size_t get_tier_size(int tier_index) {
    switch (tier_index) {
        case 0: return BUFFER_SIZE_4KB;
        case 1: return BUFFER_SIZE_16KB;
        case 2: return BUFFER_SIZE_64KB;
        case 3: return BUFFER_SIZE_256KB;
        default: return 0;
    }
}

/**
 * Create a new buffer pool
 */
httpmorph_buffer_pool_t* buffer_pool_create(void) {
    httpmorph_buffer_pool_t *pool = (httpmorph_buffer_pool_t*)calloc(1, sizeof(httpmorph_buffer_pool_t));
    if (!pool) {
        return NULL;
    }

    /* Initialize tiers */
    for (int i = 0; i < NUM_TIERS; i++) {
        pool->tiers[i].buffer_size = get_tier_size(i);
        pool->tiers[i].available_count = 0;
        memset(pool->tiers[i].buffers, 0, sizeof(pool->tiers[i].buffers));
    }

    /* Initialize statistics */
    pool->hits = 0;
    pool->misses = 0;
    pool->returns = 0;

    /* Initialize mutex */
#ifdef _WIN32
    InitializeCriticalSection(&pool->mutex);
#else
    if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
        free(pool);
        return NULL;
    }
#endif

    return pool;
}

/**
 * Destroy a buffer pool
 */
void buffer_pool_destroy(httpmorph_buffer_pool_t *pool) {
    if (!pool) {
        return;
    }

    /* Lock mutex before destroying to prevent concurrent access */
#ifdef _WIN32
    EnterCriticalSection(&pool->mutex);
#else
    pthread_mutex_lock(&pool->mutex);
#endif

    /* Free all pooled buffers while holding lock */
    for (int i = 0; i < NUM_TIERS; i++) {
        for (int j = 0; j < pool->tiers[i].available_count; j++) {
            free(pool->tiers[i].buffers[j]);
        }
    }

    /* Unlock before destroying mutex */
#ifdef _WIN32
    LeaveCriticalSection(&pool->mutex);
    DeleteCriticalSection(&pool->mutex);
#else
    pthread_mutex_unlock(&pool->mutex);
    pthread_mutex_destroy(&pool->mutex);
#endif

    free(pool);
}

/**
 * Allocate a buffer from the pool
 */
void* buffer_pool_get(httpmorph_buffer_pool_t *pool, size_t size, size_t *actual_size) {
    if (!pool || size == 0) {
        return NULL;
    }

    int tier_index = get_tier_index(size);
    void *buffer = NULL;

    /* Lock pool */
#ifdef _WIN32
    EnterCriticalSection(&pool->mutex);
#else
    pthread_mutex_lock(&pool->mutex);
#endif

    /* Try to get buffer from pool */
    if (tier_index >= 0 && pool->tiers[tier_index].available_count > 0) {
        /* Get buffer from pool */
        pool->tiers[tier_index].available_count--;
        buffer = pool->tiers[tier_index].buffers[pool->tiers[tier_index].available_count];
        pool->tiers[tier_index].buffers[pool->tiers[tier_index].available_count] = NULL;

        pool->hits++;

        if (actual_size) {
            *actual_size = pool->tiers[tier_index].buffer_size;
        }
    } else {
        /* Allocate new buffer */
        size_t alloc_size = (tier_index >= 0) ? get_tier_size(tier_index) : size;
        buffer = malloc(alloc_size);

        pool->misses++;

        if (actual_size) {
            *actual_size = alloc_size;
        }
    }

    /* Unlock pool */
#ifdef _WIN32
    LeaveCriticalSection(&pool->mutex);
#else
    pthread_mutex_unlock(&pool->mutex);
#endif

    return buffer;
}

/**
 * Return a buffer to the pool
 */
void buffer_pool_put(httpmorph_buffer_pool_t *pool, void *buffer, size_t size) {
    if (!pool || !buffer || size == 0) {
        return;
    }

    int tier_index = get_tier_index(size);

    /* Lock pool */
#ifdef _WIN32
    EnterCriticalSection(&pool->mutex);
#else
    pthread_mutex_lock(&pool->mutex);
#endif

    /* Try to return buffer to pool */
    if (tier_index >= 0 && pool->tiers[tier_index].available_count < BUFFERS_PER_TIER) {
        /* Add buffer to pool */
        pool->tiers[tier_index].buffers[pool->tiers[tier_index].available_count] = buffer;
        pool->tiers[tier_index].available_count++;

        pool->returns++;
    } else {
        /* Pool full or buffer too large - free it */
        free(buffer);
    }

    /* Unlock pool */
#ifdef _WIN32
    LeaveCriticalSection(&pool->mutex);
#else
    pthread_mutex_unlock(&pool->mutex);
#endif
}

/**
 * Get buffer pool statistics
 */
void buffer_pool_stats(httpmorph_buffer_pool_t *pool,
                       size_t *hits, size_t *misses, size_t *returns) {
    if (!pool) {
        return;
    }

    /* Lock pool */
#ifdef _WIN32
    EnterCriticalSection(&pool->mutex);
#else
    pthread_mutex_lock(&pool->mutex);
#endif

    if (hits) *hits = pool->hits;
    if (misses) *misses = pool->misses;
    if (returns) *returns = pool->returns;

    /* Unlock pool */
#ifdef _WIN32
    LeaveCriticalSection(&pool->mutex);
#else
    pthread_mutex_unlock(&pool->mutex);
#endif
}
