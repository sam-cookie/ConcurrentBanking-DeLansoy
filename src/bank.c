#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bank.h"

Bank bank;

// initialises the bank: zeroes all accounts, sets num_accounts to 0,
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

// need deposit and withdraw w pthread_rwlock_wrlock 
// need transfer w lock ordering and get_balance and w pthread_rwlock_rdlock 