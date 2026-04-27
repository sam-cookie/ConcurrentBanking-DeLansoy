#ifndef LOCK_MGR_H
#define LOCK_MGR_H

#include <stdbool.h>

typedef enum {
    DEADLOCK_PREVENTION,
    DEADLOCK_DETECTION,
} DeadlockStrategy;

extern DeadlockStrategy deadlock_strategy;

bool lm_transfer(int from_id, int to_id, int amount_centavos, int tx_id);
void lm_init(void);
void lm_destroy(void);

void lm_record_wait(int tx_id, int account_id, int holder_tx_id);
void lm_clear_wait(int tx_id);
bool lm_detect_deadlock(int *victim_tx_id);

#endif // LOCK_MGR_H