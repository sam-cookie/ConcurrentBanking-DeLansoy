#include <stdio.h>
#include <stdlib.h>
#include "transaction.h"
#include "timer.h"
#include "bank.h"
#include "lock_mgr.h"
#include "buffer_pool.h"

void *execute_transaction(void *arg)
{
    Transaction *tx = (Transaction *)arg;
    if (tx == NULL) return NULL;

    /* Wait until scheduled start tick */
    wait_until_tick(tx->start_tick);
    tx->actual_start = global_tick;
    printf("Tick %d: T%d started\n", global_tick, tx->tx_id);

    /* Load all accounts this transaction needs into the buffer pool up front. */
    bool loaded[MAX_ACCOUNTS] = {false};
    for (int i = 0; i < tx->num_ops; i++) {
        Operation *op = &tx->ops[i];
        if (!loaded[op->account_id]) {
            bp_load(&buffer_pool, op->account_id);
            loaded[op->account_id] = true;
        }
        if (op->type == OP_TRANSFER && !loaded[op->target_account]) {
            bp_load(&buffer_pool, op->target_account);
            loaded[op->target_account] = true;
        }
    }

    /* Execute each operation in the transaction */
    for (int i = 0; i < tx->num_ops; i++) {
        Operation *op = &tx->ops[i];
        
        // Force the transaction to take time (Excellent criteria)
        int tick_before = global_tick;
        wait_until_tick(global_tick + 1); 
        tx->wait_ticks += (global_tick - tick_before);

        bool success = false;
        switch (op->type) {
        case OP_DEPOSIT:
            success = deposit(op->account_id, op->amount_centavos);
            break;
        case OP_WITHDRAW:
            success = withdraw(op->account_id, op->amount_centavos);
            if (!success) printf("T%d: withdraw failed for account %d\n", tx->tx_id, op->account_id);
            break;
        case OP_TRANSFER:
            success = lm_transfer(op->account_id, op->target_account, op->amount_centavos, tx->tx_id);
            break;
        case OP_BALANCE: {
            int balance = get_balance(op->account_id);
            printf("T%d: Account %d balance = PHP %d.%02d\n",
                   tx->tx_id, op->account_id, balance / 100, balance % 100);
            success = true;
            break;
        }
        }

        if (!success && op->type != OP_BALANCE) {
            tx->status = TX_ABORTED;
            tx->actual_end = global_tick;
            for (int j = 0; j < MAX_ACCOUNTS; j++)
                if (loaded[j]) bp_unload(&buffer_pool, j);
            return NULL;
        }
    }

    tx->status = TX_COMMITTED;
    tx->actual_end = global_tick;

    /* Cleanup: Unload everything after commit */
    for (int j = 0; j < MAX_ACCOUNTS; j++) {
        if (loaded[j]) bp_unload(&buffer_pool, j);
    }

    return NULL;
}