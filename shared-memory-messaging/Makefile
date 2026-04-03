.PHONY: all clean run-demo help test test-basic test-concurrent valgrind

CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99
CFLAGS_DEBUG = -Wall -Wextra -g -std=c99
TARGET = shm
SOURCES = shm.c
HEADER = shm.h
OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^
	@echo "✓ Build successful: $(TARGET)"

%.o: %.c $(HEADER)
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f $(OBJECTS) $(TARGET)
	@echo "✓ Cleaned up object files and executable"

# Debug build with symbols and no optimization
debug: CFLAGS = $(CFLAGS_DEBUG)
debug: clean $(TARGET)
	@echo "✓ Debug build successful"

# Run comprehensive tests
test: test-basic test-concurrent
	@echo ""
	@echo "========================================="
	@echo "✓ All tests completed successfully!"
	@echo "========================================="

test-basic: all
	@echo "Running basic functionality tests..."
	@bash tests/test_basic.sh

test-concurrent: all
	@echo ""
	@echo "Running concurrent and stress tests..."
	@bash tests/test_concurrent.sh

# Valgrind memory check (requires valgrind)
valgrind: debug
	@echo "Running valgrind memory check..."
	valgrind --leak-check=full --track-origins=yes --show-leak-kinds=all \
		./$(TARGET) init 2>&1 | head -50

help:
	@echo "Message Broadcasting System - Makefile Targets"
	@echo "=============================================="
	@echo "  make all       - Build the program (default)"
	@echo "  make debug     - Build with debug symbols"
	@echo "  make clean     - Remove build artifacts"
	@echo "  make test      - Run all tests"
	@echo "  make test-basic      - Run basic tests only"
	@echo "  make test-concurrent - Run concurrent tests only"
	@echo "  make valgrind  - Check for memory leaks"
	@echo "  make run-demo  - Run a simple demo workflow"
	@echo "  make help      - Show this help message"

# Simple demo: initialize, create dialogue, list
run-demo: all
	@echo "Running demo workflow..."
	@echo "1. Initialize shared memory..."
	./$(TARGET) init
	@echo ""
	@echo "2. Create dialogue 999..."
	./$(TARGET) create 999
	@echo ""
	@echo "3. List active messages..."
	./$(TARGET) list
	@echo ""
	@echo "4. Show system status..."
	./$(TARGET) status
	@echo ""
	@echo "✓ Demo complete (run './shm cleanup' to remove IPC resources)"
