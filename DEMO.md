# System Demonstration Guide

This guide shows how to use the Shared Memory Message Broadcasting System in various scenarios.

## Quick Start

### 1. Basic Send/Receive Workflow

**Terminal 1: Initialize and Create Dialogue**
```bash
$ make all
$ ./shm init
Requesting shared memory segment with key=0x4d534700, size=5384 bytes
Initializing shared memory...
Shared memory initialized successfully!
  Mutex semaphore ID: 1234567
  Message available semaphore ID: 1234568
  Slot-available semaphore ID: 1234569

$ ./shm create 100
Dialogue 100 created by PID 12345

=== Dialogue 100 ===
Status: Active
Participants: 1
PIDs: 12345
Next sequence number: 0
```

**Terminal 2: Join and Start Listening**
```bash
$ ./shm join 100
PID 23456 joined dialogue 100

=== Dialogue 100 ===
Status: Active
Participants: 2
PIDs: 12345 23456
Next sequence number: 0

$ ./shm recv
Entering reader mode (PID 23456). Waiting for messages...
```

**Terminal 3: Send Messages**
```bash
$ ./shm send 100 "Hello from Terminal 1"
$ ./shm send 100 "This is the second message"
$ ./shm send 100 "Last message before termination"
$ ./shm send 100 "TERMINATE"
TERMINATE sent: cleaned up resources and exiting.
```

**Terminal 2 Output (after Terminal 3 sends):**
```
--- Received message for dialogue 100 ---
  From PID: 12343
  Sequence: 0
  Content: Hello from Terminal 1
  Timestamp: Thu Apr  3 15:05:32 2026

--- Received message for dialogue 100 ---
  From PID: 12343
  Sequence: 1
  Content: This is the second message
  Timestamp: Thu Apr  3 15:05:33 2026

--- Received message for dialogue 100 ---
  From PID: 12343
  Sequence: 2
  Content: Last message before termination
  Timestamp: Thu Apr  3 15:05:35 2026

--- Received message for dialogue 100 ---
  From PID: 12343
  Sequence: 3
  Content: TERMINATE
  Timestamp: Thu Apr  3 15:05:36 2026
TERMINATE message received. Exiting reader.
TERMINATE received: cleaned up resources and exiting.
```

## System Status Monitoring

### View Active Dialogues

```bash
$ ./shm list

=== Active Dialogues ===
ID: 100  |  Participants: 3  |  Messages Sent: 5
ID: 200  |  Participants: 2  |  Messages Sent: 0
(Additional dialogues...)
```

### Detailed System Status

```bash
$ ./shm status

=== Shared Memory Status ===
Dialogues:       2/10 active
Message Buffer:  3/100 slots used
Active Readers:  1

Semaphore Values:
  mutex:         1 (1=available, 0=locked)
  msg_available: 3 (messages ready to read)
  slot_available:97 (free buffer slots)

--- Active Dialogues ---
[0] ID=100  parts=3  seq=5
[1] ID=200  parts=2  seq=0
```

## Advanced Usage: Multi-Dialogue Scenario

Create and manage multiple independent dialogues with different participants:

**Terminal 1: Create Two Dialogues**
```bash
$ ./shm init
$ ./shm create 101  # Support team dialogue
$ ./shm create 102  # Dev team dialogue
```

**Terminal 2: Join Support Dialogue and Listen**
```bash
$ ./shm join 101
$ ./shm recv
Entering reader mode (PID 99999). Waiting for messages...
```

**Terminal 3: Join Dev Dialogue and Listen**
```bash
$ ./shm join 102
$ ./shm recv
Entering reader mode (PID 88888). Waiting for messages...
```

**Terminal 4: Send to Support**
```bash
$ ./shm send 101 "Issue reported: System A down"
$ ./shm send 101 "ETA for fix: 30 minutes"
```

**Terminal 5: Send to Dev Team**
```bash
$ ./shm send 102 "PR #123 ready for review"
$ ./shm send 102 "New feature: User auth v2"
```

**Results:**
- Terminal 2 sees only 101 messages
- Terminal 3 sees only 102 messages
- Each dialogue maintains independent message ordering

## Running Automated Tests

```bash
# Run all tests
$ make test
Running basic functionality tests...
✓ PASS: init
✓ PASS: create dialogue 999
✓ PASS: join dialogue 999
✓ PASS: send message
✓ PASS: list command
✓ PASS: status command
✓ PASS: TERMINATE message

Running concurrent and stress tests...
✓ PASS: concurrent send/recv
✓ PASS: message ordering (sequence numbers)

=========================================
✓ All concurrent tests passed!
=========================================
```

## Debugging and Troubleshooting

### Check Current IPC Resources

```bash
$ ipcs -m  # Show all shared memory segments
$ ipcs -s  # Show all semaphores
```

### Manual Cleanup (if process crashes)

```bash
# Remove shared memory segment
$ ipcrm -m 0x4d534700

# Remove semaphores
$ ipcrm -s 0x4d534701 0x4d534702 0x4d534703
```

### Run with Valgrind (Memory Leak Detection)

```bash
$ make valgrind
```

### Build with Debug Symbols

```bash
$ make debug
$ gdb ./shm
```

## Performance Characteristics

- **Throughput**: ~100 messages/sec (depends on system load)
- **Latency**: < 1ms for message delivery
- **Buffer Capacity**: 100 messages × 256 bytes = ~26 KB
- **Max Dialogues**: 10
- **Max Participants per Dialogue**: 20

## Key Design Insights (for Interview Discussion)

1. **Circular Buffer**: Efficient fixed-size queue with wraparound
2. **Read Bitmap**: Tracks which participants read each message (20-bit map for 20 participants)
3. **Three-Semaphore Design**: 
   - Mutex for critical sections
   - Message-available counter for producers/consumers
   - Slot-available counter for bounded queue

4. **Exact-Once Delivery**: Each participant reads each message exactly once
5. **Automatic Cleanup**: Dead processes detected via `kill(pid, 0)` check

## Next Steps

1. Try the basic demo: `make run-demo`
2. Run stress tests: `make test`
3. Explore with multiple terminals: `./shm join 100` in one, `./shm recv` in another
4. Monitor with: `./shm status` in a third terminal
5. Observe cleanup behavior after TERMINATE

---

For more information, see [README.md](README.md)
