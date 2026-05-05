#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include "bank.h"
#include "timer.h"
#include "lock_mgr.h"
#include "transaction.h"
#include "metrics.h"

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

    // Initialize timer with specified tick interval
    tick_interval_ms = tick_ms;
    timer_init();

    // Parse accounts file
    int num_accounts = parse_accounts(accounts_file);
    if (num_accounts < 0) {
        die("Failed to parse accounts file");
    }
    if (verbose) {
        printf("Loaded %d accounts\n", num_accounts);
    }

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
    simulation_running = 0;
    pthread_cond_broadcast(&tick_changed);

    // Print transaction details
    printf("\n=== Transaction Summary ===\n");
    metrics_print_table(transactions, num_transactions);

    // Compute and print metrics
    printf("\n=== Metrics ===\n");
    Metrics m = metrics_compute(transactions, num_transactions, global_tick);
    metrics_print_summary(&m);

    // Print final bank state
    printf("\n=== Final Bank State ===\n");
    long long total_balance = bank_total_balance();
    printf("Total balance: PHP %lld.%02lld\n", total_balance / 100, total_balance % 100);

    // Cleanup
    bank_destroy();
    lm_destroy();
    timer_destroy();

    return 0;
}