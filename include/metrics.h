#ifndef METRICS_H
#define METRICS_H

#include "transaction.h"
#include "buffer_pool.h"

// structure to hold performance metrics for the banking system
// includes transaction stats and buffer pool usage data
typedef struct {
    int    total_transactions;  // total number of transactions processed
    int    committed;           // number of transactions that succeeded
    int    aborted;             // number of transactions that failed
    int    total_ticks;         // total simulation time in ticks
    double avg_wait_ticks;      // average time transactions waited
    double throughput;          // transactions completed per tick

    // buffer pool performance statistics
    int    bp_total_loads;      // total accounts loaded into buffer
    int    bp_total_unloads;    // total accounts removed from buffer
    int    bp_peak_usage;       // maximum buffer slots used at once
    int    bp_blocked_ops;      // operations blocked due to full buffer
} Metrics;

Metrics metrics_compute(Transaction *txs, int num_txs, int total_ticks);
void    metrics_print_table(Transaction *txs, int num_txs);
void    metrics_print_summary(const Metrics *m);

#endif // METRICS_H 