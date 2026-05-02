#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>

#include "bank.h"
#include "transaction.h"

// prints msg + errno string to stderr
void perror_die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

// prints a formatted message to stderr
void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}

// returns true if line is empty or starts with # 
static int is_blank_or_comment(const char *line)
{
    while (*line && isspace((unsigned char)*line))
        line++;
    return (*line == '\0' || *line == '#');
}

// reads accounts.txt and calls bank_create_account() for each valid line
// format: AccountID  InitialBalanceCentavos
// returns number of accounts loaded, or -1 on error
int parse_accounts(const char *filename)
{
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "parse_accounts: cannot open '%s': %s\n",
                filename, strerror(errno));
        return -1;
    }

    char line[256];
    int  lineno = 0;
    int  loaded = 0;

    while (fgets(line, sizeof(line), fp)) {
        lineno++;
        line[strcspn(line, "\n")] = '\0';

        if (is_blank_or_comment(line))
            continue;

        int account_id;
        int balance;

        if (sscanf(line, "%d %d", &account_id, &balance) != 2) {
            fprintf(stderr,
                    "parse_accounts: %s:%d: bad format (expected 'ID BALANCE'): %s\n",
                    filename, lineno, line);
            fclose(fp);
            return -1;
        }

        if (account_id < 0 || account_id >= MAX_ACCOUNTS) {
            fprintf(stderr,
                    "parse_accounts: %s:%d: account_id %d out of range [0, %d)\n",
                    filename, lineno, account_id, MAX_ACCOUNTS);
            fclose(fp);
            return -1;
        }

        if (balance < 0) {
            fprintf(stderr,
                    "parse_accounts: %s:%d: negative balance %d for account %d\n",
                    filename, lineno, balance, account_id);
            fclose(fp);
            return -1;
        }

        if (!bank_create_account(account_id, balance)) {
            fprintf(stderr,
                    "parse_accounts: %s:%d: failed to create account %d "
                    "(already exists?)\n",
                    filename, lineno, account_id);
            fclose(fp);
            return -1;
        }

        loaded++;
    }

    if (ferror(fp)) {
        perror("parse_accounts: read error");
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return loaded;
}

// parses a "TN" token (e.g. "T1", "T42") and returns the numeric part
// returns -1 if the token does not match the expected pattern
static int parse_tx_token(const char *tok)
{
    if (tok[0] != 'T' && tok[0] != 't')
        return -1;
    char *end;
    long id = strtol(tok + 1, &end, 10);
    if (*end != '\0' || id < 0 || id >= MAX_TRANSACTIONS)
        return -1;
    return (int)id;
}

// finds an existing Transaction slot by tx_id, or allocates a new one
// initialises the slot on first creation and increments *num_txs
// returns NULL if MAX_TRANSACTIONS is exceeded
static Transaction *find_or_alloc_tx(Transaction *txs, int *num_txs, int tx_id)
{
    for (int i = 0; i < *num_txs; i++) {
        if (txs[i].tx_id == tx_id)
            return &txs[i];
    }

    if (*num_txs >= MAX_TRANSACTIONS) {
        fprintf(stderr, "parse_trace: too many transactions (max %d)\n",
                MAX_TRANSACTIONS);
        return NULL;
    }

    Transaction *tx = &txs[*num_txs];
    memset(tx, 0, sizeof(*tx));
    tx->tx_id        = tx_id;
    tx->start_tick   = -1;
    tx->actual_start = -1;
    tx->actual_end   = -1;
    tx->wait_ticks   = 0;
    tx->status       = TX_PENDING;
    tx->num_ops      = 0;

    (*num_txs)++;
    return tx;
}

// reads a trace file and populates the txs array with parsed transactions
// format per line: TxID  StartTick  OPERATION  AccountID  [args...]
int parse_trace(const char *filename, Transaction *txs, int *num_txs)
{
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "parse_trace: cannot open '%s': %s\n",
                filename, strerror(errno));
        return -1;
    }

    *num_txs = 0;

    char line[512];
    int  lineno = 0;

    while (fgets(line, sizeof(line), fp)) {
        lineno++;
        line[strcspn(line, "\n")] = '\0';

        if (is_blank_or_comment(line))
            continue;

        char  copy[512];
        char *saveptr = NULL;
        strncpy(copy, line, sizeof(copy) - 1);
        copy[sizeof(copy) - 1] = '\0';

        char *tok_txid = strtok_r(copy, " \t", &saveptr);
        char *tok_tick = strtok_r(NULL, " \t", &saveptr);
        char *tok_op   = strtok_r(NULL, " \t", &saveptr);
        char *tok_acc  = strtok_r(NULL, " \t", &saveptr);

        if (!tok_txid || !tok_tick || !tok_op || !tok_acc) {
            fprintf(stderr, "parse_trace: %s:%d: too few fields: '%s'\n",
                    filename, lineno, line);
            fclose(fp);
            return -1;
        }

        // parse and validate TxID
        int tx_id = parse_tx_token(tok_txid);
        if (tx_id < 0) {
            fprintf(stderr, "parse_trace: %s:%d: invalid transaction id '%s'\n",
                    filename, lineno, tok_txid);
            fclose(fp);
            return -1;
        }

        // parse and validate StartTick
        char *end_tick;
        int start_tick = (int)strtol(tok_tick, &end_tick, 10);
        if (*end_tick != '\0' || start_tick < 0) {
            fprintf(stderr, "parse_trace: %s:%d: invalid start tick '%s'\n",
                    filename, lineno, tok_tick);
            fclose(fp);
            return -1;
        }

        // get or create the transaction slot for this TxID
        Transaction *tx = find_or_alloc_tx(txs, num_txs, tx_id);
        if (!tx) {
            fclose(fp);
            return -1;
        }

        // set start_tick on first occurrence; warn if a later line contradicts it
        if (tx->start_tick == -1) {
            tx->start_tick = start_tick;
        } else if (tx->start_tick != start_tick) {
            fprintf(stderr,
                    "parse_trace: %s:%d: warning: T%d start tick changed "
                    "from %d to %d (ignoring)\n",
                    filename, lineno, tx_id, tx->start_tick, start_tick);
        }

        if (tx->num_ops >= MAX_OPS_PER_TX) {
            fprintf(stderr, "parse_trace: %s:%d: T%d exceeds max ops (%d)\n",
                    filename, lineno, tx_id, MAX_OPS_PER_TX);
            fclose(fp);
            return -1;
        }

        Operation *op = &tx->ops[tx->num_ops];
        memset(op, 0, sizeof(*op));

        // normalise operation keyword to uppercase for case-insensitive matching
        for (char *p = tok_op; *p; p++)
            *p = (char)toupper((unsigned char)*p);

        // Parse and validate AccountID
        char *end_acc;
        int account_id = (int)strtol(tok_acc, &end_acc, 10);
        if (*end_acc != '\0' || account_id < 0 || account_id >= MAX_ACCOUNTS) {
            fprintf(stderr, "parse_trace: %s:%d: invalid account id '%s'\n",
                    filename, lineno, tok_acc);
            fclose(fp);
            return -1;
        }
        op->account_id = account_id;

        if (strcmp(tok_op, "DEPOSIT") == 0) {
            op->type = OP_DEPOSIT;

            // DEPOSIT requires one additional argument: amount
            char *tok_amt = strtok_r(NULL, " \t", &saveptr);
            if (!tok_amt) {
                fprintf(stderr, "parse_trace: %s:%d: DEPOSIT missing amount\n",
                        filename, lineno);
                fclose(fp);
                return -1;
            }
            char *end_amt;
            int amount = (int)strtol(tok_amt, &end_amt, 10);
            if (*end_amt != '\0' || amount <= 0) {
                fprintf(stderr, "parse_trace: %s:%d: DEPOSIT invalid amount '%s'\n",
                        filename, lineno, tok_amt);
                fclose(fp);
                return -1;
            }
            op->amount_centavos = amount;

        } else if (strcmp(tok_op, "WITHDRAW") == 0) {
            op->type = OP_WITHDRAW;

            // WITHDRAW requires one additional argument: amount
            char *tok_amt = strtok_r(NULL, " \t", &saveptr);
            if (!tok_amt) {
                fprintf(stderr, "parse_trace: %s:%d: WITHDRAW missing amount\n",
                        filename, lineno);
                fclose(fp);
                return -1;
            }
            char *end_amt;
            int amount = (int)strtol(tok_amt, &end_amt, 10);
            if (*end_amt != '\0' || amount <= 0) {
                fprintf(stderr, "parse_trace: %s:%d: WITHDRAW invalid amount '%s'\n",
                        filename, lineno, tok_amt);
                fclose(fp);
                return -1;
            }
            op->amount_centavos = amount;

        } else if (strcmp(tok_op, "TRANSFER") == 0) {
            op->type = OP_TRANSFER;

            // TRANSFER requires two additional arguments: target account then amount
            // tok_acc already holds the source (from_id)
            char *tok_to  = strtok_r(NULL, " \t", &saveptr);
            char *tok_amt = strtok_r(NULL, " \t", &saveptr);

            if (!tok_to || !tok_amt) {
                fprintf(stderr,
                        "parse_trace: %s:%d: TRANSFER missing target/amount\n",
                        filename, lineno);
                fclose(fp);
                return -1;
            }

            char *end_to;
            int to_id = (int)strtol(tok_to, &end_to, 10);
            if (*end_to != '\0' || to_id < 0 || to_id >= MAX_ACCOUNTS) {
                fprintf(stderr, "parse_trace: %s:%d: TRANSFER invalid target '%s'\n",
                        filename, lineno, tok_to);
                fclose(fp);
                return -1;
            }

            char *end_amt;
            int amount = (int)strtol(tok_amt, &end_amt, 10);
            if (*end_amt != '\0' || amount <= 0) {
                fprintf(stderr, "parse_trace: %s:%d: TRANSFER invalid amount '%s'\n",
                        filename, lineno, tok_amt);
                fclose(fp);
                return -1;
            }

            op->target_account  = to_id;
            op->amount_centavos = amount;

        } else if (strcmp(tok_op, "BALANCE") == 0) {
            // BALANCE requires no additional arguments
            op->type = OP_BALANCE;

        } else {
            fprintf(stderr, "parse_trace: %s:%d: unknown operation '%s'\n",
                    filename, lineno, tok_op);
            fclose(fp);
            return -1;
        }

        tx->num_ops++;
    }

    if (ferror(fp)) {
        perror("parse_trace: read error");
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

// returns the value portion of a --key=value argument, or NULL if it does not match
const char *arg_value(const char *arg, const char *key)
{
    size_t klen = strlen(key);
    if (strncmp(arg, key, klen) != 0)
        return NULL;
    if (arg[klen] != '=')
        return NULL;
    return arg + klen + 1;
}

// prints usage information to stderr and exits with code 1
void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s --accounts=FILE --trace=FILE\n"
        "          --deadlock=prevention|detection\n"
        "          [--tick-ms=N]  (default 100)\n"
        "          [--verbose]\n",
        prog);
    exit(EXIT_FAILURE);
}