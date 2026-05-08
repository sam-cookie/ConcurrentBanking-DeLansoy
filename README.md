# Concurrent Banking System

A multithreaded banking simulation that runs concurrent transactions with deadlock prevention, a bounded buffer pool, and reader-writer locks.

## Compilation

Requires gcc and pthreads.

```sh
make          # production build
make debug    # debug build with ThreadSanitizer
make clean    # remove build artifacts
```

## Usage

```sh
./bankdb --accounts=<file> --trace=<file> [--deadlock=prevention|detection] [--tick-ms=N] [--verbose]
```

Arguments:

- `--accounts` -- path to accounts file (one account per line: `ID BalanceCentavos`)
- `--trace` -- path to trace file (one operation per line: `TxID StartTick OP ACCT [TARGET] AMOUNT`)
- `--deadlock` -- deadlock handling strategy (default: prevention)
- `--tick-ms` -- tick interval in milliseconds (default: 100)
- `--verbose` -- print extra diagnostic output

### Example

```sh
./bankdb --accounts=tests/accounts.txt --trace=tests/trace_simple.txt
```

### Test suite

```sh
make test
```

Runs five test cases: no conflicts, concurrent readers, deadlock prevention, insufficient funds abort, and buffer pool saturation.

## Features

- **Deadlock prevention** via global lock ordering (always acquire locks in ascending account ID order)
- **Bounded buffer pool** with semaphore-based slot management (pool size: 5)
- **Reader-writer locks** on accounts -- multiple balance checks can run concurrently
- **Timer thread** providing a global tick for timing and synchronization
- **Multi-operation transactions** -- a single transaction can run deposit, withdraw, and balance checks sequentially
- **Execution log** showing per-tick events (starts, completions, lock acquisitions)
- **Metrics** -- committed/aborted counts, per-transaction timing table, throughput, buffer pool statistics

## Known limitations

- Deadlock detection mode (`--deadlock=detection`) is not fully implemented. The wait-for graph tracking functions are stubs, so the system will fall back to prevention behavior regardless of the flag.
- All lock acquisition happens sequentially inside `lm_transfer()`, so the log cannot show interleaved lock waits the way a real deadlock detection system would.
- Trace files cannot have multiple transactions with the same ID, since each ID maps to exactly one thread.
- The buffer pool uses a naive producer-consumer pattern with two semaphores -- this works but can be inefficient under high contention.
