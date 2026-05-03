// Deadlock prevention or detection

#include <stdio.h>
#include <stdlib.h>
#include "lock_mgr.h"
#include "bank.h"

DeadlockStrategy deadlock_strategy = DEADLOCK_PREVENTION;

void lm_init(void)
{
    /* Initialize lock manager */
}

void lm_destroy(void)
{
    /* Clean up lock manager */
}

bool lm_transfer(int from_id, int to_id, int amount_centavos, int tx_id)
{
    (void)tx_id;

    /* Validate account IDs */
    if (from_id < 0 || from_id >= MAX_ACCOUNTS ||
        to_id < 0 || to_id >= MAX_ACCOUNTS ||
        from_id == to_id) {
        return false;
    }

    if (amount_centavos < 0) {
        return false;
    }

    /* Lock ordering: always acquire locks in ascending account ID order */
    int first_id = from_id < to_id ? from_id : to_id;
    int second_id = from_id < to_id ? to_id : from_id;

    Account *first_acc = &bank.accounts[first_id];
    Account *second_acc = &bank.accounts[second_id];

    pthread_rwlock_wrlock(&first_acc->lock);
    pthread_rwlock_wrlock(&second_acc->lock);

    /* Check sufficient balance on source account */
    Account *from_acc = &bank.accounts[from_id];
    if (from_acc->balance_centavos < amount_centavos) {
        pthread_rwlock_unlock(&second_acc->lock);
        pthread_rwlock_unlock(&first_acc->lock);
        return false;
    }

    /* Perform transfer */
    from_acc->balance_centavos -= amount_centavos;
    Account *to_acc = &bank.accounts[to_id];
    to_acc->balance_centavos += amount_centavos;

    pthread_rwlock_unlock(&second_acc->lock);
    pthread_rwlock_unlock(&first_acc->lock);
    return true;
}

void lm_record_wait(int tx_id, int account_id, int holder_tx_id)
{
    (void)tx_id;
    (void)account_id;
    (void)holder_tx_id;
    /* Wait-for graph tracking (used for deadlock detection mode) */
}

void lm_clear_wait(int tx_id)
{
    (void)tx_id;
    /* Clear wait state (used for deadlock detection mode) */
}

bool lm_detect_deadlock(int *victim_tx_id)
{
    (void)victim_tx_id;
    /* Deadlock detection via wait-for graph (future iteration) */
    return false;
}