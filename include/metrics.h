#ifndef METRICS_H
#define METRICS_H

#include "transaction.h"

typedef struct {
    int    total_transactions;
    int    committed;
    int    aborted;
    int    total_ticks;
    double avg_wait_ticks;
    double throughput;          /* tx / tick */
} Metrics;

Metrics metrics_compute(Transaction *txs, int num_txs, int total_ticks);
void    metrics_print_table(Transaction *txs, int num_txs);
void    metrics_print_summary(const Metrics *m);

#endif // METRICS_H 