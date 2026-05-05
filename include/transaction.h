#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <pthread.h>

#define MAX_TRANSACTIONS 256
#define MAX_OPS_PER_TX   256

typedef enum {
    OP_DEPOSIT,   // Add money to account
    OP_WITHDRAW,  // Remove money from account
    OP_TRANSFER,  // Move money between two accounts
    OP_BALANCE,   // Read account balance
} OpType;

typedef struct {
    OpType type;
    int account_id;          // Primary account
    int amount_centavos;     // Amount in centavos
    int target_account;      // For TRANSFER only
} Operation;

typedef enum {
    TX_PENDING,
    TX_RUNNING,
    TX_COMMITTED,
    TX_ABORTED,
} TxStatus;

typedef struct {
    int        tx_id;
    Operation  ops[MAX_OPS_PER_TX];
    int        num_ops;
    int        start_tick;
    pthread_t  thread;

    /* timing (in ticks) */
    int        actual_start;
    int        actual_end;
    int        wait_ticks;

    TxStatus   status;
} Transaction;

// Execute a single transaction in a thread
void *execute_transaction(void *arg);

#endif // TRANSACTION_H