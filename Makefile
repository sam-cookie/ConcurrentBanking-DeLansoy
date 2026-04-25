CC      = gcc
# Added -pthread for threading support and -D_POSIX_C_SOURCE for timers
CFLAGS  = -Wall -Wextra -Wpedantic -g -Iinclude -pthread
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

# ── Build ────────────────────────────────────────────────────────────
all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run-simple: $(TARGET)
	./$(TARGET) --accounts=tests/accounts.txt --trace=tests/trace_simple.txt

run-readers: $(TARGET)
	./$(TARGET) --accounts=tests/accounts.txt --trace=tests/trace_readers.txt

run-deadlock: $(TARGET)
	./$(TARGET) --accounts=tests/accounts.txt --trace=tests/trace_deadlock.txt

run-buffer: $(TARGET)
	./$(TARGET) --accounts=tests/accounts.txt --trace=tests/trace_buffer.txt

run-abort: $(TARGET)
	./$(TARGET) --accounts=tests/accounts.txt --trace=tests/trace_abort.txt

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean run-simple run-readers run-deadlock run-buffer