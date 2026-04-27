#ifndef BUFFER_POOL_H
#define BUFFER_POOL_H

#include <semaphore.h>
#include <pthread.h>
#include <stdbool.h>

#define BUFFER_POOL_SIZE 5

typedef struct {
    int      account_id;   /* -1 = slot is free */
    bool     in_use;
} BufferSlot;

typedef struct {
    BufferSlot      slots[BUFFER_POOL_SIZE];
    sem_t           empty_slots;   /* counts free slots  */
    sem_t           full_slots;    /* counts used slots  */
    pthread_mutex_t pool_lock;

    /* statistics */
    int total_loads;
    int total_unloads;
    int peak_usage;
    int current_usage;
    int blocked_ops;
} BufferPool;

/* Global buffer pool (defined in buffer_pool.c) */
extern BufferPool buffer_pool;

void bp_init(BufferPool *pool);
void bp_load(BufferPool *pool, int account_id);
void bp_unload(BufferPool *pool, int account_id);
bool bp_is_loaded(const BufferPool *pool, int account_id);
void bp_print_stats(const BufferPool *pool);
void bp_destroy(BufferPool *pool);

#endif // buffer_pool.h