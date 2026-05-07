// this file calculates and displays performance metrics
// includes transaction statistics and buffer pool usage

#include <stdio.h>
#include <stdlib.h>
#include "metrics.h"
#include "buffer_pool.h"

// reference to the global buffer pool for stats
extern BufferPool buffer_pool;

// calculates overall metrics from transaction data and total time
// computes averages, throughput, and includes buffer pool stats
Metrics metrics_compute(Transaction *txs, int num_txs, int total_ticks)
{
    // initialize metrics structure to zero
    Metrics m = {0};

    // set basic counts
    m.total_transactions = num_txs;
    m.total_ticks = total_ticks;

    // accumulate wait times and count committed/aborted
    int total_wait_ticks = 0;

    for (int i = 0; i < num_txs; i++) {
        // check transaction status
        if (txs[i].status == TX_COMMITTED) {
            m.committed++;
        } else if (txs[i].status == TX_ABORTED) {
            m.aborted++;
        }
        // add up all wait times
        total_wait_ticks += txs[i].wait_ticks;
    }

    // calculate average wait time if there were transactions
    if (num_txs > 0) {
        m.avg_wait_ticks = (double)total_wait_ticks / num_txs;
    }

    // calculate throughput: successful transactions per tick
    if (total_ticks > 0) {
        m.throughput = (double)m.committed / total_ticks;
    }

    // copy buffer pool statistics
    m.bp_total_loads = buffer_pool.total_loads;
    m.bp_total_unloads = buffer_pool.total_unloads;
    m.bp_peak_usage = buffer_pool.peak_usage;
    m.bp_blocked_ops = buffer_pool.blocked_ops;

    return m;
}

// prints a table showing details for each transaction
// displays timing, status, and operation count
void metrics_print_table(Transaction *txs, int num_txs)
{
    // print table header
    printf("%-5s | %-9s | %-11s | %-3s | %-9s | %-10s\n",
           "TxID", "StartTick", "ActualStart", "End", "WaitTicks", "Status");
    printf("------|-----------|-------------|-----|-----------|----------\n");

    // print each transaction's data
    for (int i = 0; i < num_txs; i++) {
        // convert status enum to string
        const char *status_str;
        switch (txs[i].status) {
        case TX_PENDING: status_str = "PENDING"; break;
        case TX_RUNNING: status_str = "RUNNING"; break;
        case TX_COMMITTED: status_str = "COMMITTED"; break;
        case TX_ABORTED: status_str = "ABORTED"; break;
        default: status_str = "UNKNOWN"; break;
        }

        // print transaction details with pipe separators
        printf("T%-4d | %9d | %11d | %3d | %9d | %-10s\n",
               txs[i].tx_id,
               txs[i].start_tick,
               txs[i].actual_start,
               txs[i].actual_end,
               txs[i].wait_ticks,
               status_str);
    }
}

// prints summary metrics including buffer pool stats
// shows overall system performance
void metrics_print_summary(const Metrics *m)
{
    // print transaction summary
    printf("total transactions: %d\n", m->total_transactions);
    printf("committed: %d\n", m->committed);
    printf("aborted: %d\n", m->aborted);
    printf("total ticks: %d\n", m->total_ticks);
    printf("average wait ticks: %.2f\n", m->avg_wait_ticks);
    printf("throughput (tx/tick): %.4f\n", m->throughput);

    // print buffer pool section
    printf("\nbuffer pool stats:\n");
    printf("  total loads: %d\n", m->bp_total_loads);
    printf("  total unloads: %d\n", m->bp_total_unloads);
    printf("  peak usage: %d\n", m->bp_peak_usage);
    printf("  blocked ops: %d\n", m->bp_blocked_ops);
}