// this file implements a bounded buffer pool for managing account loading
// it uses semaphores to control access and prevent overflow
// the buffer simulates memory constraints for accounts

#include <stdio.h>
#include <stdlib.h>
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
    sem_getvalue(&pool->empty_slots, &sval);
    if (sval <= 0) {
        pthread_mutex_lock(&pool->pool_lock);
        pool->blocked_ops++;
        pthread_mutex_unlock(&pool->pool_lock);
    }

    // wait for an empty slot in the buffer
    sem_wait(&pool->empty_slots);

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

    // update peak usage metric
    if (current_usage + 1 > pool->peak_usage) {
        pool->peak_usage = current_usage + 1;
    }

    pthread_mutex_unlock(&pool->pool_lock);

    // signal that there is now a new full slot
    sem_post(&pool->full_slots);
}

// removes a specific account from the buffer, freeing its slot
// finds the exact account slot by account_id before touching any semaphore
void bp_unload(BufferPool *pool, int account_id)
{
    // lock first to find and clear the specific account's slot atomically
    pthread_mutex_lock(&pool->pool_lock);

    bool found = false;
    for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
        if (pool->slots[i].account_id == account_id && pool->slots[i].in_use) {
            pool->slots[i].account_id = -1;  // mark as free
            pool->slots[i].in_use = false;
            pool->current_usage--;           // one less slot used
            pool->total_unloads++;           // count unload operations
            found = true;
            break;
        }
    }

    pthread_mutex_unlock(&pool->pool_lock);

    // only adjust semaphores if we actually found and removed the account
    // this prevents corrupting the semaphore count when the account
    // was never in the pool (e.g. double-unload or wrong account_id)
    if (found) {
        // restore one empty slot — lets a blocked bp_load proceed
        sem_post(&pool->empty_slots);
    }
}

// checks if a specific account is currently loaded in the buffer
// returns true if found, false otherwise
bool bp_is_loaded(const BufferPool *pool, int account_id)
{
    // scan all slots to see if account is present
    for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
        if (pool->slots[i].account_id == account_id && pool->slots[i].in_use) {
            return true;  // found it
        }
    }
    return false;  // not found
}

// prints out the current buffer pool statistics
// shows usage counts and performance metrics
void bp_print_stats(const BufferPool *pool)
{
    printf("buffer pool stats:\n");
    printf("  total loads: %d\n", pool->total_loads);
    printf("  total unloads: %d\n", pool->total_unloads);
    printf("  peak usage: %d\n", pool->peak_usage);
    printf("  blocked ops: %d\n", pool->blocked_ops);
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