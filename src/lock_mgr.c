// Deadlock prevention or detection

#include <stdio.h>
#include <stdlib.h>
#include "lock_mgr.h"
#include "bank.h"
#include "buffer_pool.h"

DeadlockStrategy deadlock_strategy = DEADLOCK_PREVENTION;

void lm_init(void)
{
    /* Initialize lock manager */
}

void lm_destroy(void)
{
    /* Clean up lock manager */
}

// transfers money between two accounts with deadlock prevention
// uses ordered locking and buffer pool for both accounts
bool lm_transfer(int from_id, int to_id, int amount_centavos, int tx_id) {
    // check that account ids are valid and different
    if (from_id < 0 || from_id >= MAX_ACCOUNTS ||
        to_id < 0 || to_id >= MAX_ACCOUNTS ||
        from_id == to_id) {
        return false;
    }

    // ensure amount is positive
    if (amount_centavos < 0) {
        return false;
    }

    // load both accounts into buffer pool in ascending id order
    // this prevents deadlocks in buffer loading
    int first_id = from_id < to_id ? from_id : to_id;
    int second_id = from_id < to_id ? to_id : from_id;

    // MANDATORY LOG FOR LAB MANUAL:
    if (from_id > to_id) {
        printf("[DEADLOCK PREVENTED] T%d: Lock ordering applied for accounts %d and %d\n", 
                tx_id, first_id, second_id);
    }

    Account *first_acc = &bank.accounts[first_id];
    Account *second_acc = &bank.accounts[second_id];

    // acquire write locks on both accounts
    pthread_rwlock_wrlock(&first_acc->lock);
    pthread_rwlock_wrlock(&second_acc->lock);

    // verify source account has enough money
    Account *from_acc = &bank.accounts[from_id];
    if (from_acc->balance_centavos < amount_centavos) {
        // unlock if insufficient funds
        pthread_rwlock_unlock(&second_acc->lock);
        pthread_rwlock_unlock(&first_acc->lock);
        return false;
    }

    // perform the transfer: subtract from source, add to destination
    from_acc->balance_centavos -= amount_centavos;
    Account *to_acc = &bank.accounts[to_id];
    to_acc->balance_centavos += amount_centavos;

    // unlock the accounts
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