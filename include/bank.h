#ifndef BANK_H
#define BANK_H

#include <pthread.h>
#include <stdbool.h>

#define MAX_ACCOUNTS 100

// account

typedef struct {
    int              account_id;
    int              balance_centavos;
    pthread_rwlock_t lock;
    bool             exists;
} Account;

// bank

typedef struct {
    Account         accounts[MAX_ACCOUNTS];
    int             num_accounts;
    pthread_mutex_t bank_lock;
} Bank;

extern Bank bank;

bool      bank_create_account(int account_id, int initial_balance_centavos);
void      bank_init(void);
void      bank_destroy(void);
long long bank_total_balance(void);

bool deposit(int account_id, int amount_centavos);
bool withdraw(int account_id, int amount_centavos);
int  get_balance(int account_id);

#endif // BANK_H