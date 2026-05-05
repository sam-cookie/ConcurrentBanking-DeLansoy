
#include <stdio.h>
#include <stdlib.h>
#include "transaction.h"
#include "timer.h"
#include "bank.h"
#include "lock_mgr.h"

void *execute_transaction(void *arg)
{
    Transaction *tx = (Transaction *)arg;
    if (tx == NULL) {
        return NULL;
    }

    /* Wait until scheduled start tick */
    wait_until_tick(tx->start_tick);
    tx->actual_start = global_tick;

    /* Execute each operation in the transaction */
    for (int i = 0; i < tx->num_ops; i++) {
        Operation *op = &tx->ops[i];

        int tick_before = global_tick;
        bool success = false;

        switch (op->type) {
        case OP_DEPOSIT:
            success = deposit(op->account_id, op->amount_centavos);
            if (!success) {
                fprintf(stderr, "T%d: deposit failed for account %d\n",
                        tx->tx_id, op->account_id);
                tx->status = TX_ABORTED;
                return NULL;
            }
            break;

        case OP_WITHDRAW:
            success = withdraw(op->account_id, op->amount_centavos);
            if (!success) {
                fprintf(stderr, "T%d: withdraw failed for account %d\n",
                        tx->tx_id, op->account_id);
                tx->status = TX_ABORTED;
                return NULL;
            }
            break;

        case OP_TRANSFER:
            success = lm_transfer(op->account_id, op->target_account,
                                  op->amount_centavos, tx->tx_id);
            if (!success) {
                fprintf(stderr, "T%d: transfer failed from %d to %d\n",
                        tx->tx_id, op->account_id, op->target_account);
                tx->status = TX_ABORTED;
                return NULL;
            }
            break;

        case OP_BALANCE: {
            int balance = get_balance(op->account_id);
            printf("T%d: Account %d balance = PHP %d.%02d\n",
                   tx->tx_id, op->account_id,
                   balance / 100, balance % 100);
            success = true;
            break;
        }

        default:
            fprintf(stderr, "T%d: unknown operation type %d\n",
                    tx->tx_id, op->type);
            tx->status = TX_ABORTED;
            return NULL;
        }

        /* Track wait time across ticks */
        tx->wait_ticks += (global_tick - tick_before);
    }

    /* Transaction completed successfully */
    tx->actual_end = global_tick;
    tx->status = TX_COMMITTED;
    return NULL;
}