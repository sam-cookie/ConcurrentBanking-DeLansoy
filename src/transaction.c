#include <stdio.h>
#include "transaction.h"
#include "timer.h"
#include "bank.h"
#include "lock_mgr.h"
#include "buffer_pool.h"

static int last_logged_tick = -1;

static const char *op_name(OpType t)
{
    switch (t) {
    case OP_DEPOSIT:  return "DEPOSIT";
    case OP_WITHDRAW: return "WITHDRAW";
    case OP_TRANSFER: return "TRANSFER";
    case OP_BALANCE:  return "BALANCE";
    default:          return "UNKNOWN";
    }
}

#define LOG_TICK(tick) \
    do { \
        if ((tick) != last_logged_tick) { \
            if (last_logged_tick >= 0) printf("\n"); \
            last_logged_tick = (tick); \
        } \
    } while (0)

void *execute_transaction(void *arg)
{
    Transaction *tx = (Transaction *)arg;
    if (tx == NULL) return NULL;

    /* Wait until scheduled start tick */
    wait_until_tick(tx->start_tick);
    int current_tick = get_current_tick();
    tx->actual_start = current_tick;
    
    // Print transaction start with operation details
    Operation *op = &tx->ops[0];
    const char *op_type_str = op_name(op->type);

    LOG_TICK(current_tick);
    if (op->type == OP_TRANSFER) {
        printf("Tick %d:\n  T%d started: %s from %d to %d amount PHP %d.%02d\n",
               current_tick, tx->tx_id, op_type_str, op->account_id, op->target_account,
               op->amount_centavos / 100, op->amount_centavos % 100);
    } else if (op->type == OP_BALANCE) {
        printf("Tick %d:\n  T%d started: %s account %d\n",
               current_tick, tx->tx_id, op_type_str, op->account_id);
    } else {
        printf("Tick %d:\n  T%d started: %s account %d amount PHP %d.%02d\n",
               current_tick, tx->tx_id, op_type_str, op->account_id,
               op->amount_centavos / 100, op->amount_centavos % 100);
    }

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
        
        const char *op_type_str = op_name(op->type);

        // Force the transaction to take time (Excellent criteria)
        int tick_before = get_current_tick();
        wait_until_tick(tick_before + 1); 
        tx->wait_ticks += (get_current_tick() - tick_before);

        int op_tick = get_current_tick();
        bool success = false;
        switch (op->type) {
        case OP_DEPOSIT:
            success = deposit(op->account_id, op->amount_centavos);
            if (success) {
                LOG_TICK(op_tick);
                printf("Tick %d:\n  T%d completed: %s successful\n",
                       op_tick, tx->tx_id, op_type_str);
            }
            break;
        case OP_WITHDRAW:
            success = withdraw(op->account_id, op->amount_centavos);
            if (success) {
                LOG_TICK(op_tick);
                printf("Tick %d:\n  T%d completed: %s successful\n",
                       op_tick, tx->tx_id, op_type_str);
            } else {
                printf("  T%d: withdraw failed for account %d\n", tx->tx_id, op->account_id);
            }
            break;
        case OP_TRANSFER:
            success = lm_transfer(op->account_id, op->target_account, op->amount_centavos, tx->tx_id);
            if (success) {
                LOG_TICK(op_tick);
                printf("Tick %d:\n  T%d completed: %s successful\n",
                       op_tick, tx->tx_id, op_type_str);
            }
            break;
        case OP_BALANCE: {
            int balance = get_balance(op->account_id);
            LOG_TICK(op_tick);
            printf("Tick %d:\n  T%d: Account %d balance = PHP %d.%02d\n",
                   op_tick, tx->tx_id, op->account_id,
                   balance / 100, balance % 100);
            success = true;
            break;
        }
        }

        if (!success && op->type != OP_BALANCE) {
            tx->status = TX_ABORTED;
            tx->actual_end = get_current_tick();
            for (int j = 0; j < MAX_ACCOUNTS; j++)
                if (loaded[j]) bp_unload(&buffer_pool, j);
            return NULL;
        }
    }

    tx->status = TX_COMMITTED;
    tx->actual_end = get_current_tick();

    /* Cleanup: Unload everything after commit */
    for (int j = 0; j < MAX_ACCOUNTS; j++) {
        if (loaded[j]) bp_unload(&buffer_pool, j);
    }

    return NULL;
}