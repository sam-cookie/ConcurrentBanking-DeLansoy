CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -O2 -Iinclude -pthread
LDFLAGS = -pthread -lm
TARGET  = bankdb

SRC = src/main.c           \
      src/bank.c           \
      src/transaction.c    \
      src/timer.c          \
      src/lock_mgr.c       \
      src/buffer_pool.c    \
      src/metrics.c        \
      src/utils.c

OBJ = $(SRC:.c=.o)

# build 
all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

#debug build with sanitizers
debug: CFLAGS = -Wall -Wextra -Wpedantic -g -fsanitize=thread -Iinclude -pthread
debug: clean $(TARGET)

# testing
test: $(TARGET)
	@echo "=== Test 1: No Conflicts ==="
	./$(TARGET) --accounts=tests/accounts.txt --trace=tests/trace_simple.txt --deadlock=prevention --tick-ms=100
	@echo ""
	@echo "=== Test 2: Concurrent Readers ==="
	./$(TARGET) --accounts=tests/accounts.txt --trace=tests/trace_readers.txt --deadlock=prevention --tick-ms=100
	@echo ""
	@echo "=== Test 3: Deadlock Prevention ==="
	./$(TARGET) --accounts=tests/accounts.txt --trace=tests/trace_deadlock.txt --deadlock=prevention --tick-ms=100
	@echo ""
	@echo "=== Test 4: Insufficient Funds ==="
	./$(TARGET) --accounts=tests/accounts.txt --trace=tests/trace_abort.txt --deadlock=prevention --tick-ms=100
	@echo ""
	@echo "=== Test 5: Buffer Pool Saturation ==="
	./$(TARGET) --accounts=tests/accounts.txt --trace=tests/trace_buffer.txt --deadlock=prevention --tick-ms=100

# individual runs
run-simple: $(TARGET)
	./$(TARGET) --accounts=tests/accounts.txt --trace=tests/trace_simple.txt --deadlock=prevention

run-readers: $(TARGET)
	./$(TARGET) --accounts=tests/accounts.txt --trace=tests/trace_readers.txt --deadlock=prevention

run-deadlock: $(TARGET)
	./$(TARGET) --accounts=tests/accounts.txt --trace=tests/trace_deadlock.txt --deadlock=prevention

run-buffer: $(TARGET)
	./$(TARGET) --accounts=tests/accounts.txt --trace=tests/trace_buffer.txt --deadlock=prevention

run-abort: $(TARGET)
	./$(TARGET) --accounts=tests/accounts.txt --trace=tests/trace_abort.txt --deadlock=prevention

#  clean
clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all debug test clean run-simple run-readers run-deadlock run-buffer run-abort