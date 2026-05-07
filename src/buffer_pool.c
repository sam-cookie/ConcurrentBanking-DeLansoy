// this file implements a bounded buffer pool for managing account loading
// it uses semaphores to control access and prevent overflow
// the buffer simulates memory constraints for accounts

#include <stdio.h>
#include <string.h>
#include <semaphore.h>
#include <pthread.h>
#include <stdbool.h>

#include "buffer_pool.h"

// global buffer pool instance used across the system
BufferPool buffer_pool;

// sets up the buffer pool with initial empty state
// initializes semaphores for thread-safe operations
// prepares statistics tracking
void bp_init(BufferPool *pool)
{
    // clear all memory to zero
    memset(pool, 0, sizeof(BufferPool));

    // mark all buffer slots as free and unused
    for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
        pool->slots[i].account_id = -1;  // -1 means slot is free
        pool->slots[i].in_use = false;   // not currently holding an account
    }

    // set up semaphores: empty_slots starts at max (all free)
    // full_slots starts at 0 (none occupied)
    sem_init(&pool->empty_slots, 0, BUFFER_POOL_SIZE);
    sem_init(&pool->full_slots, 0, 0);
    // mutex protects the buffer slots array
    pthread_mutex_init(&pool->pool_lock, NULL);

    // reset all statistics counters
    pool->total_loads = 0;
    pool->total_unloads = 0;
    pool->peak_usage = 0;
    pool->current_usage = 0;
    pool->blocked_ops = 0;
}

// loads an account into the buffer, blocking if buffer is full
// waits for a free slot, then assigns the account to it
// updates usage statistics
void bp_load(BufferPool *pool, int account_id)
{
    // Check if buffer is full before waiting to track blocked ops
    int sval;
    bool was_blocked = false;
    sem_getvalue(&pool->empty_slots, &sval);
    if (sval <= 0) {
        was_blocked = true;
        printf("  [BUFFER FULL] Account %d is blocked, waiting for a free slot...\n", account_id);
        pthread_mutex_lock(&pool->pool_lock);
        pool->blocked_ops++;
        pthread_mutex_unlock(&pool->pool_lock);
    }

    // wait for an empty slot in the buffer
    sem_wait(&pool->empty_slots);

    if (was_blocked) {
        printf("  [BUFFER SLOT FREE] Account %d has acquired a slot and is proceeding\n", account_id);
    }

    pthread_mutex_lock(&pool->pool_lock);

    // update statistics
    pool->total_loads++;

    // find a free slot or verify if already loaded
    int slot_idx = -1;
    int current_usage = 0;
    for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
        if (pool->slots[i].in_use) {
            current_usage++;
        }
        if (slot_idx == -1 && !pool->slots[i].in_use) {
            slot_idx = i;
        }
    }

    // place account in the found slot
    pool->slots[slot_idx].account_id = account_id;
    pool->slots[slot_idx].in_use = true;

    // update current and peak usage
    pool->current_usage = current_usage + 1;
    if (pool->current_usage > pool->peak_usage) {
        pool->peak_usage = pool->current_usage;
    }

    pthread_mutex_unlock(&pool->pool_lock);

    // signal that there is now a new full slot
    sem_post(&pool->full_slots);
}

// removes a specific account from the buffer, freeing its slot
// consumer side of the producer-consumer pattern: waits for a full slot
void bp_unload(BufferPool *pool, int account_id)
{
    // wait for a full slot before attempting to unload
    sem_wait(&pool->full_slots);

    // lock to find and clear the specific account's slot atomically
    pthread_mutex_lock(&pool->pool_lock);

    for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
        if (pool->slots[i].account_id == account_id && pool->slots[i].in_use) {
            pool->slots[i].account_id = -1;  // mark as free
            pool->slots[i].in_use = false;
            pool->current_usage--;           // one less slot used
            pool->total_unloads++;           // count unload operations
            break;
        }
    }

    pthread_mutex_unlock(&pool->pool_lock);

    // restore one empty slot — lets a blocked bp_load proceed
    sem_post(&pool->empty_slots);
}

// cleans up the buffer pool resources
// destroys semaphores and mutex
void bp_destroy(BufferPool *pool)
{
    // clean up synchronization objects
    sem_destroy(&pool->empty_slots);
    sem_destroy(&pool->full_slots);
    pthread_mutex_destroy(&pool->pool_lock);
}