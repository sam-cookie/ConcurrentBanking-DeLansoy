#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bank.h"
#include "lock_mgr.h"
#include "timer.h"
#include "buffer_pool.h"

Bank bank;

// initialises the bank: zeroes all accounts, sets num_accounts to 0,
// and initialises the bank-level mutex
void bank_init(void)
{
    memset(&bank, 0, sizeof(bank));

    for (int i = 0; i < MAX_ACCOUNTS; i++) {
        bank.accounts[i].exists     = false;
        bank.accounts[i].account_id = -1;
    }

    if (pthread_mutex_init(&bank.bank_lock, NULL) != 0) {
        perror("bank_init: pthread_mutex_init");
        exit(EXIT_FAILURE);
    }
}

// creates an account with the given ID and initial balance
// returns false if account_id is out of range or already exists
bool bank_create_account(int account_id, int initial_balance_centavos)
{
    if (account_id < 0 || account_id >= MAX_ACCOUNTS)
        return false;

    pthread_mutex_lock(&bank.bank_lock);

    if (bank.accounts[account_id].exists) {
        pthread_mutex_unlock(&bank.bank_lock);
        return false;
    }

    Account *acc = &bank.accounts[account_id];
    acc->account_id       = account_id;
    acc->balance_centavos = initial_balance_centavos;
    acc->exists           = true;

    if (pthread_rwlock_init(&acc->lock, NULL) != 0) {
        pthread_mutex_unlock(&bank.bank_lock);
        perror("bank_create_account: pthread_rwlock_init");
        exit(EXIT_FAILURE);
    }

    bank.num_accounts++;

    pthread_mutex_unlock(&bank.bank_lock);
    return true;
}

// returns the sum of all account balances in centavos
// used for the conservation check before and after all transactions
long long bank_total_balance(void)
{
    long long total = 0;

    pthread_mutex_lock(&bank.bank_lock);

    for (int i = 0; i < MAX_ACCOUNTS; i++) {
        if (bank.accounts[i].exists)
            total += bank.accounts[i].balance_centavos;
    }

    pthread_mutex_unlock(&bank.bank_lock);
    return total;
}

// destroys all per-account reader-writer locks and the bank-level mutex
void bank_destroy(void)
{
    for (int i = 0; i < MAX_ACCOUNTS; i++) {
        if (bank.accounts[i].exists)
            pthread_rwlock_destroy(&bank.accounts[i].lock);
    }

    pthread_mutex_destroy(&bank.bank_lock);
}

// adds amount to account balance using a write lock
// measures ticks spent waiting for the lock and returns them to the caller
// now includes buffer pool loading to simulate memory access
bool deposit(int account_id, int amount_centavos)
{
    // check if account id is valid
    if (account_id < 0 || account_id >= MAX_ACCOUNTS)
        return false;

    // get pointer to the account
    Account *acc = &bank.accounts[account_id];

    // make sure account exists
    if (!acc->exists)
        return false;

    // load account into buffer pool if not already there
    // this simulates loading from disk/memory
    if (!bp_is_loaded(&buffer_pool, account_id)) {
        bp_load(&buffer_pool, account_id);
    }

    // record tick before locking to measure wait time
    int tick_before = global_tick;
    // acquire write lock on the account
    pthread_rwlock_wrlock(&acc->lock);
    // record tick after locking
    int tick_after = global_tick;

    // add the amount to the balance
    acc->balance_centavos += amount_centavos;

    // release the lock
    pthread_rwlock_unlock(&acc->lock);

    // unload the account from buffer after operation
    bp_unload(&buffer_pool, account_id);

    // calculate wait ticks (available for metrics)
    (void)(tick_after - tick_before);
    return true;
}

// removes amount from account balance using a write lock
// returns false if account does not exist or has insufficient funds
// includes buffer pool management for memory simulation
bool withdraw(int account_id, int amount_centavos)
{
    // validate account id range
    if (account_id < 0 || account_id >= MAX_ACCOUNTS)
        return false;

    // get account pointer
    Account *acc = &bank.accounts[account_id];

    if (!acc->exists)
        return false;

    // ensure account is loaded in buffer pool
    if (!bp_is_loaded(&buffer_pool, account_id)) {
        bp_load(&buffer_pool, account_id);
    }

    // measure lock wait time
    int tick_before = global_tick;
    // lock for writing
    pthread_rwlock_wrlock(&acc->lock);
    int tick_after = global_tick;

    // check if balance is sufficient
    if (acc->balance_centavos < amount_centavos) {
        // unlock and unload if insufficient funds
        pthread_rwlock_unlock(&acc->lock);
        bp_unload(&buffer_pool, account_id);
        return false;
    }

    // subtract the amount
    acc->balance_centavos -= amount_centavos;
    // unlock
    pthread_rwlock_unlock(&acc->lock);

    // remove from buffer after operation
    bp_unload(&buffer_pool, account_id);

    // wait ticks calculated
    (void)(tick_after - tick_before);
    return true;
}

// reads account balance using a read lock (allows concurrent readers)
// returns -1 if account does not exist
// uses buffer pool to simulate memory access
int get_balance(int account_id)
{
    // check valid id
    if (account_id < 0 || account_id >= MAX_ACCOUNTS)
        return -1;

    // get account
    Account *acc = &bank.accounts[account_id];

    // ensure exists
    if (!acc->exists)
        return -1;

    // load into buffer if needed
    if (!bp_is_loaded(&buffer_pool, account_id)) {
        bp_load(&buffer_pool, account_id);
    }

    // measure wait time
    int tick_before = global_tick;
    // read lock (shared)
    pthread_rwlock_rdlock(&acc->lock);
    int tick_after = global_tick;

    // get the balance
    int balance = acc->balance_centavos;
    // unlock
    pthread_rwlock_unlock(&acc->lock);

    // unload from buffer
    bp_unload(&buffer_pool, account_id);

    // wait ticks
    (void)(tick_after - tick_before);
    return balance;
}