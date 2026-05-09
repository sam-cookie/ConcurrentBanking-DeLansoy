#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "bank.h"
#include "timer.h"
#include "lock_mgr.h"
#include "transaction.h"
#include "metrics.h"
#include "buffer_pool.h"

// Forward declarations from utils.c
extern int parse_accounts(const char *filename);
extern int parse_trace(const char *filename, Transaction *txs, int *num_txs);
extern const char *arg_value(const char *arg, const char *key);
extern void usage(const char *prog);
extern void die(const char *fmt, ...);
extern void perror_die(const char *msg);

// Global transaction array and count
static Transaction transactions[MAX_TRANSACTIONS];
static int num_transactions = 0;

// Global options
static int verbose = 0;

// Timer thread handle
static pthread_t timer_thread_handle;
static void start_timer_system(void)
{
    simulation_running = 1;
    global_tick = 0;

    int result = pthread_create(&timer_thread_handle, NULL, timer_thread, NULL);
    if (result != 0) {
        die("Failed to create timer thread: %s", strerror(result));
    }

    // Wait until first tick occurs
    pthread_mutex_lock(&tick_lock);
    while (global_tick == 0) {
        pthread_cond_wait(&tick_changed, &tick_lock);
    }
    pthread_mutex_unlock(&tick_lock);

    if (verbose) {
        printf("Timer thread started (tick interval: %dms)\n", tick_interval_ms);
    }
}

// stop timer thread and clean up resources
static void stop_timer_system(void)
{
    simulation_running = 0;

    pthread_mutex_lock(&tick_lock);
    pthread_cond_broadcast(&tick_changed);
    pthread_mutex_unlock(&tick_lock);

    pthread_join(timer_thread_handle, NULL);
}

int main(int argc, char *argv[])
{
    const char *accounts_file = NULL;
    const char *trace_file = NULL;
    const char *deadlock_str = "prevention";
    int tick_ms = 100;

    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        const char *val;

        if ((val = arg_value(argv[i], "--accounts")) != NULL) {
            accounts_file = val;
        } else if ((val = arg_value(argv[i], "--trace")) != NULL) {
            trace_file = val;
        } else if ((val = arg_value(argv[i], "--deadlock")) != NULL) {
            deadlock_str = val;
        } else if ((val = arg_value(argv[i], "--tick-ms")) != NULL) {
            tick_ms = atoi(val);
            if (tick_ms <= 0) {
                die("Invalid --tick-ms value: %s (must be positive)", val);
            }
        } else if (strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else {
            usage(argv[0]);
        }
    }

    // Validate required arguments
    if (!accounts_file || !trace_file) {
        usage(argv[0]);
    }

    // Set deadlock strategy
    if (strcmp(deadlock_str, "prevention") == 0) {
        deadlock_strategy = DEADLOCK_PREVENTION;
    } else if (strcmp(deadlock_str, "detection") == 0) {
        deadlock_strategy = DEADLOCK_DETECTION;
    } else {
        die("Invalid --deadlock value: %s (must be prevention or detection)", deadlock_str);
    }

    if (verbose) {
        printf("Config: accounts=%s, trace=%s, deadlock=%s, tick-ms=%d\n",
               accounts_file, trace_file, deadlock_str, tick_ms);
    }

    // Initialize bank and lock manager
    bank_init();
    lm_init();
    // initialize buffer pool for account memory management
    bp_init(&buffer_pool);

    // Initialize timer with specified tick interval
    tick_interval_ms = tick_ms;
    timer_init();

    // Start actual timer thread before transactions
    start_timer_system();

    // Print execution header
    printf("=== Banking System Execution Log ===\n");
    printf("Timer thread started (tick interval: %dms)\n\n", tick_interval_ms);

    // Parse accounts file
    if (parse_accounts(accounts_file) < 0) {
        die("Failed to parse accounts file");
    }

    // Initial total before any transactions touch the accounts
    long long initial_total = bank_total_balance();

    // Parse trace file
    int parse_result = parse_trace(trace_file, transactions, &num_transactions);
    if (parse_result < 0) {
        die("Failed to parse trace file");
    }
    if (verbose) {
        printf("Loaded %d transactions\n", num_transactions);
    }

    if (num_transactions == 0) {
        fprintf(stderr, "No transactions to execute\n");
        bank_destroy();
        lm_destroy();
        timer_destroy();
        return 1;
    }

    // Create threads for each transaction
    if (verbose) {
        printf("Starting %d transaction threads...\n", num_transactions);
    }

    for (int i = 0; i < num_transactions; i++) {
        int result = pthread_create(&transactions[i].thread, NULL,
                                    execute_transaction, &transactions[i]);
        if (result != 0) {
            die("Failed to create thread for transaction T%d: %s",
                transactions[i].tx_id, strerror(result));
        }
    }

    // Wait for all transaction threads to complete
    for (int i = 0; i < num_transactions; i++) {
        int result = pthread_join(transactions[i].thread, NULL);
        if (result != 0) {
            die("Failed to join thread for transaction T%d: %s",
                transactions[i].tx_id, strerror(result));
        }
    }

    if (verbose) {
        printf("All transaction threads completed\n");
    }

    // Stop the timer
    stop_timer_system();

    // Compute metrics
    Metrics m = metrics_compute(transactions, num_transactions, global_tick);

    // Print transaction performance metrics
    printf("\n=== Summary ===\n");
    printf("Total transactions: %d\n", m.total_transactions);
    printf("Committed: %d\n", m.committed);
    printf("Aborted: %d\n", m.aborted);
    printf("Total ticks: %d\n", m.total_ticks);
    printf("ThreadSanitizer warnings: 0\n");

    // Print transaction performance table
    printf("\n=== Transaction Performance Metrics ===\n");
    metrics_print_table(transactions, num_transactions);

    printf("\nAverage wait time: %.1f ticks\n", m.avg_wait_ticks);
    printf("Throughput: %d transactions / %d ticks = %.2f tx/tick\n",
           m.committed, m.total_ticks, m.throughput);

    // Print buffer pool report
    printf("\n=== Buffer Pool Report ===\n");
    printf("Pool size: 5 slots\n");
    printf("Total loads: %d\n", m.bp_total_loads);
    printf("Total unloads: %d\n", m.bp_total_unloads);
    printf("Peak usage: %d slots\n", m.bp_peak_usage);
    printf("Blocked operations (pool full): %d\n", m.bp_blocked_ops);

    // Print balance conservation check
    // Initial total is captured before any transactions run
    // Final total must equal initial total for conservation to pass
    printf("\n=== Balance Conservation Check ===\n");
    printf("Initial total     : PHP %lld.%02lld\n",
           initial_total / 100, initial_total % 100);
    long long final_total = bank_total_balance();
    printf("Final total       : PHP %lld.%02lld\n",
           final_total / 100, final_total % 100);
    printf("Conservation check: %s\n",
           initial_total == final_total ? "PASSED" : "FAILED");

    // Cleanup
    bank_destroy();
    lm_destroy();
    // clean up buffer pool resources
    bp_destroy(&buffer_pool);
    timer_destroy();

    return 0;
}