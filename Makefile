.PHONY: all clean run-demo help

CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99
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

help:
	@echo "Message Broadcasting System - Makefile Targets"
	@echo "=============================================="
	@echo "  make all       - Build the program (default)"
	@echo "  make clean     - Remove build artifacts"
	@echo "  make run-demo  - Run a simple demo (init + dialogue workflow)"
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
	@echo "✓ Demo complete (run ./shm cleanup to remove IPC resources)"
