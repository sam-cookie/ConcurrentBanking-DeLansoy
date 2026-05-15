# 1. Deadlock Strategy Choice
## Choice: Strategy A (Deadlock Prevention)
>  **Deadlock Prevention** via *Global Lock Ordering.*

### Why this strategy?
We opted for prevention because it stops the problem before it starts. Instead of letting the system freeze and then hunting for cycles to break, prevention ensures a smooth, predictable flow for every transaction. It's more efficient for a banking system where we want to avoid the "stop-and-roll-back" overhead that comes with deadlock detection.

### Breaking the Circular Wait
The program eliminates the Circular Wait condition (one of the four Coffman conditions). We enforce a rule: every transaction must acquire locks in ascending order of account_id.

### The Proof: 
Imagine Transaction A wants to move money from Account 20 to 10, while Transaction B wants to move money from 10 to 20. 
- Without ordering, they could each grab one lock and wait forever for the other.

- With the ordering logic, both transactions are forced to ask for Account 10 first. Transaction B gets the lock, and Transaction A waits patiently for 10 without holding 20. Since nobody can "hold a high ID while waiting for a low ID," a cycle can never form.

# 2. Buffer Pool Integration
### Lifecycle Management
- **Loading:** We load every account a transaction needs right at the start. Before a single centavo moves, *bp_load()* checks the accounts into the pool.

- **Unloading:** We hold those accounts in the pool for the entire life of the transaction. We only call *bp_unload()* once the transaction successfully commits or aborts.

### What happens when the pool is full?
The buffer pool has 5 slots. If a 6th account is needed, the transaction hits a ***semaphore*** (sem_wait). This blocks the thread entirely. It stays suspended until another transaction finishes and frees up a slot via sem_post.

### Justification for this Design
This **"Hold-until-End"** design is intentional. It forces the system to respect the physical resource limits we’ve set. In Test 5, you can see this in action: because transactions don't let go of their slots until they are totally done, the 6th transaction is forced to wait. This proves the bounded buffer is actually doing its job—protecting the system from memory exhaustion.s

**Test 5:**
```
=== Test 5: Buffer Pool Saturation ===
./bankdb --accounts=tests/accounts.txt --trace=tests/trace_buffer.txt --deadlock=prevention --tick-ms=100
=== Banking System Execution Log ===
Timer thread started (tick interval: 100ms)

Tick 1:
  T1 started: DEPOSIT account 1 amount PHP 10.00
Tick 1:
  T2 started: DEPOSIT account 2 amount PHP 10.00
Tick 1:
  T3 started: DEPOSIT account 3 amount PHP 10.00
Tick 1:
  T4 started: DEPOSIT account 4 amount PHP 10.00
Tick 1:
  T5 started: DEPOSIT account 5 amount PHP 10.00
Tick 1:
  T6 started: DEPOSIT account 6 amount PHP 10.00
  [BUFFER FULL] Account 6 is blocked, waiting for a free slot...


Tick 2:
  T2 completed: DEPOSIT successful

  [BUFFER SLOT FREE] Account 6 has acquired a slot and is proceeding
Tick 2:
  T3 completed: DEPOSIT successful
Tick 2:
  T5 completed: DEPOSIT successful

Tick 2:
  T1 completed: DEPOSIT successful

Tick 2:
  T4 completed: DEPOSIT successful

Tick 3:
  T6 completed: DEPOSIT successful

=== Summary ===
Total transactions: 6
Committed: 6
Aborted: 0
Total ticks: 4
ThreadSanitizer warnings: 0

=== Transaction Performance Metrics ===
TxID  | StartTick | ActualStart | End | WaitTicks | Status    
------|-----------|-------------|-----|-----------|----------
T1    |         0 |           1 |   2 |         1 | COMMITTED 
T2    |         0 |           1 |   2 |         1 | COMMITTED 
T3    |         0 |           1 |   2 |         1 | COMMITTED 
T4    |         0 |           1 |   2 |         1 | COMMITTED 
T5    |         0 |           1 |   2 |         1 | COMMITTED 
T6    |         0 |           1 |   3 |         1 | COMMITTED 

Average wait time: 1.0 ticks
Throughput: 6 transactions / 4 ticks = 1.50 tx/tick

=== Buffer Pool Report ===
Pool size: 5 slots
Total loads: 6
Total unloads: 6
Peak usage: 5 slots
Blocked operations (pool full): 1

=== Balance Conservation Check ===
Initial total     : PHP 1150.00
Final total       : PHP 1210.00
Conservation check: FAILED
```
# 3. Reader-Writer Lock Performance
## The Benchmark: RW-Locks vs. Mutexes
**Running trace_readers.txt using both synchronization methods. Here is what happened:**
| Metrics | Plain Mutex (pthread_mutex_t) | RW-Lock (pthread_rwlock_t) |
| :--- | :---: | ---: |
| Total Ticks | ~5 Ticks | 2 Ticks |
| Execution | Sequential (One by one) | Concurrent (All at once) |

### Why RW-Locks won
The trace_readers.txt workload is "read-heavy"—it’s full of balance inquiries.

- A Mutex is stubborn; it only lets one person look at the bank book at a time.

- A Reader-Writer Lock is smarter. It realizes that ten people can read the same balance at the same time without causing any trouble.

By allowing multiple concurrent reads, the RW-lock effectively cut our execution time in half for this workload.

# 4. Timer Thread Design
### Why a separate thread?
If we did not have the Timer Thread, each transaction would just happen as fast as they possibly could. We need a separate, independent timer to provide a consistent ***"Global Tick"*** that doesn't depend on how fast the CPU is.

### What would break without it?
If we removed the timer and just ran things sequentially:

- **Concurrency would be an illusion:** You’d never have two transactions actually fighting for the same lock at the same time.

- **Infinite Throughput:** Transactions would finish in 0 ticks because the CPU is too fast. We wouldn't be able to measure WaitTicks or Average Throughput.

### True Concurrency Testing
The timer thread is what makes this a real-world simulation. By forcing transactions to wait for a specific tick, we create *"rush hours" where* multiple threads try to grab the same accounts at the *exact same moment*. This is the only way to prove that our locks, semaphores, and deadlock prevention actually work under pressure.