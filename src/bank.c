#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bank.h"
#include "lock_mgr.h"
#include "timer.h"

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
// measures ticks spent waiting for the lock
bool deposit(int account_id, int amount_centavos) {
    if (account_id < 0 || account_id >= MAX_ACCOUNTS) return false;
    Account *acc = &bank.accounts[account_id];
    if (!acc->exists) return false;

    pthread_rwlock_wrlock(&acc->lock);
    acc->balance_centavos += amount_centavos;
    pthread_rwlock_unlock(&acc->lock);

    return true;
}

// removes amount from account balance using a write lock
// returns false if account does not exist or has insufficient funds
bool withdraw(int account_id, int amount_centavos) {
    if (account_id < 0 || account_id >= MAX_ACCOUNTS) return false;
    Account *acc = &bank.accounts[account_id];
    if (!acc->exists) return false;

    pthread_rwlock_wrlock(&acc->lock);
    if (acc->balance_centavos < amount_centavos) {
        pthread_rwlock_unlock(&acc->lock);
        return false;
    }
    acc->balance_centavos -= amount_centavos;
    pthread_rwlock_unlock(&acc->lock);

    return true;
}

// reads account balance using a read lock (allows concurrent readers)
// returns -1 if account does not exist
int get_balance(int account_id) {
    if (account_id < 0 || account_id >= MAX_ACCOUNTS) return -1;
    Account *acc = &bank.accounts[account_id];
    if (!acc->exists) return -1;

    pthread_rwlock_rdlock(&acc->lock);
    int balance = acc->balance_centavos;
    pthread_rwlock_unlock(&acc->lock);

    return balance;
}

// transfers amount from one account to another using lock ordering
// locks are always acquired in ascending account_id order to prevent deadlock
// (breaks the circular-wait Coffman condition)
// returns false if from_id has insufficient funds
bool transfer(int from_id, int to_id, int amount_centavos)
{
    if (from_id < 0 || from_id >= MAX_ACCOUNTS) return false;
    if (to_id   < 0 || to_id   >= MAX_ACCOUNTS) return false;
    if (!bank.accounts[from_id].exists)          return false;
    if (!bank.accounts[to_id].exists)            return false;


    // acquire locks in ascending ID order (deadlock prevention)
    int first  = (from_id < to_id) ? from_id : to_id;
    int second = (from_id < to_id) ? to_id   : from_id;

    pthread_rwlock_wrlock(&bank.accounts[first].lock);
    pthread_rwlock_wrlock(&bank.accounts[second].lock);

    if (bank.accounts[from_id].balance_centavos < amount_centavos) {
        pthread_rwlock_unlock(&bank.accounts[second].lock);
        pthread_rwlock_unlock(&bank.accounts[first].lock);
        return false;
    }

    bank.accounts[from_id].balance_centavos -= amount_centavos;
    bank.accounts[to_id].balance_centavos   += amount_centavos;

    pthread_rwlock_unlock(&bank.accounts[second].lock);
    pthread_rwlock_unlock(&bank.accounts[first].lock);

    return true;
}