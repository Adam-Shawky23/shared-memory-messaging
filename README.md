# Shared Memory Message Broadcasting System

A concurrent message passing system using System V IPC (shared memory and semaphores) written in C. This project demonstrates advanced OS concepts including process synchronization, inter-process communication (IPC), and concurrent access patterns.

## Overview

This is a multi-process message broadcasting system where multiple processes can create dialogues, join existing dialogues, and exchange messages in real-time. The system ensures thread-safe access to shared resources using semaphore-based synchronization and implements a circular buffer for efficient message storage.

### Key Features

- **Multi-dialogue support**: Create and manage multiple independent dialogues
- **Dynamic participant management**: Processes can join existing dialogues
- **Synchronized message passing**: Uses semaphores (binary mutex + counting semaphores) for thread-safe operations
- **Circular buffer**: Efficient memory usage with bounded message queue
- **Read tracking**: Each message includes a bitmap to track which participants have read it
- **Automatic resource cleanup**: Dead processes are detected and removed automatically
- **Cross-platform**: Works on both Linux and macOS with platform-specific adjustments

## System Architecture

### Shared Memory Layout

```
SharedMemory
├── messages[MAX_MESSAGES]         // Circular buffer of messages
├── dialogues[MAX_DIALOGUES]       // Dialogue metadata
├── message_write_index            // Write pointer for circular buffer
├── num_active_dialogues           // Count of active dialogues
├── active_reader_count            // Count of active reader processes
├── mutex_semid                    // Binary semaphore (mutual exclusion)
├── msg_available_semid            // Counting semaphore (message availability)
└── slot_available_semid           // Counting semaphore (free slots)
```

### Synchronization Primitives

- **Mutex Semaphore**: Protects shared state during critical sections
- **Message Available Semaphore**: Tracks available messages for readers (0-MAX_MESSAGES)
- **Slot Available Semaphore**: Tracks free slots in circular buffer

### Data Structures

#### Message
```c
typedef struct {
    int dialogue_id;              // ID of the dialogue this message belongs to
    pid_t sender_pid;             // Process ID of sender
    char content[256];            // Message content
    int sequence_number;          // Sequential number within dialogue
    time_t timestamp;             // When message was sent
    int is_active;                // Whether slot is in use
    unsigned int read_bitmap;     // Bitmask tracking which participants read it
    int total_readers_expected;   // Number of expected readers
} Message;
```

#### Dialogue
```c
typedef struct {
    int dialogue_id;              // Unique dialogue identifier
    int num_participants;         // Current number of participants
    pid_t participant_pids[];     // Array of participant process IDs
    int is_active;                // Whether dialogue is active
    int next_sequence_number;     // Counter for message ordering
} Dialogue;
```

## Building the Project

### Prerequisites
- GCC compiler
- POSIX-compliant system (Linux/macOS)

### Build

```bash
# Build the executable
make all

# View available make targets
make help

# Run demo workflow
make run-demo

# Clean build artifacts
make clean
```

## Usage

The system is controlled via command-line interface with the following commands:

### Initialize Shared Memory
```bash
./shm init
```
Sets up the shared memory segment and semaphores for IPC.

### Create a Dialogue
```bash
./shm create <dialogue_id>
```
Creates a new dialogue with the given ID. The creating process becomes the first participant.

**Example:**
```bash
./shm create 100
```

### Join a Dialogue
```bash
./shm join <dialogue_id>
```
Adds the current process as a participant to an existing dialogue.

**Example:**
```bash
./shm join 100
```

### Send a Message
```bash
./shm send <dialogue_id> "<message>"
```
Broadcasts a message to all participants in the dialogue.

**Example:**
```bash
./shm send 100 "Hello, world!"
```

**Special Message - TERMINATE:**
```bash
./shm send <dialogue_id> "TERMINATE"
```
Sends a termination signal that triggers cleanup of IPC resources and exits all readers.

### Receive Messages
```bash
./shm recv
# or
./shm reader
```
Starts the receiver loop. The process automatically joins all active dialogues and waits for messages.

## Workflow Example

### Terminal 1: Initialize System and Create Dialogue
```bash
# Initialize
./shm init

# Create dialogue 999
./shm create 999
```

### Terminal 2: Join and Start Receiving
```bash
# Join the dialogue
./shm join 999

# Start receiving messages
./shm recv
```

### Terminal 3: Send Messages
```bash
# Join the dialogue (good practice)
./shm join 999

# Send some messages
./shm send 999 "First message"
./shm send 999 "Second message"

# Terminate the session
./shm send 999 "TERMINATE"
```

## Implementation Details

### Circular Buffer Management
- The `messages` array acts as a circular buffer with `message_write_index` as the write pointer
- When searching for a free slot, the system finds the first inactive message
- The write pointer wraps around when reaching the end of the array

### Read Tracking
- Each message maintains a `read_bitmap` where bit N indicates whether participant N has read it
- Uses `__builtin_popcount()` to count readers
- When all expected readers have seen a message, the slot is freed

### Process Death Handling
- `leave_dialogue()` uses `kill(pid, 0)` to detect dead processes
- Dead processes are automatically removed from participant lists
- This prevents old creators from blocking cleanup

### TERMINATE Protocol
1. Sender broadcasts TERMINATE message
2. Readers receive message and exit cleanly
3. Last exiting reader removes IPC resources
4. Ensures clean shutdown without resource leaks

## Architecture Diagram

```
Process 1          Process 2          Process 3
   |                  |                  |
   |---> shm init <---|                  |
   |                  |                  |
   |-- create dlg ---|                  |
   |     dlg 100      |                  |
   |                  |---> join dlg <---|
   |                  |     dlg 100      |
   |                  |                  |--> recv
   |                  |                  | (waiting)
   |-- send msg ------|---------------> | (received)
   |                  |                  |
   |-- send TERM -----|---> TERMINATE    |
   | (cleans IPC)     |     (exits)      |
```

## Performance Characteristics

- **Message Capacity**: 100 messages max
- **Dialogue Capacity**: 10 active dialogues max
- **Participant Capacity**: 20 participants per dialogue max
- **Message Size**: 256 bytes (configurable via MAX_CONTENT_LENGTH)

## Error Handling

The system handles various error conditions:
- **No space in buffer**: Operation fails with error message
- **Non-existent dialogue**: Returns -1 and prints error
- **Full participant list**: Rejects join operation
- **Semaphore operations**: Exits with descriptive error on failure
- **Dead processes**: Automatically cleaned up

## Troubleshooting

### "shmget failed: EINVAL"
- Check for existing segment with different size
- Use `ipcrm -m key` to remove old segments
- Run `make clean` and rebuild

### Messages not being received
- Ensure receiver has joined the dialogue
- Check that receiver is running before messages are sent
- Verify dialogue ID matches between sender and receiver

### Zombie resources
- Processes terminated with `TERMINATE` message should clean up IPC
- Manual cleanup: `ipcrm -m 1234` and `ipcrm -s 5678`

## Design Decisions

1. **System V IPC**: Chosen for persistence and established semantics
2. **Circular Buffer**: Efficient for fixed-size queues with predictable memory usage
3. **Read Bitmap**: Allows exact tracking of which participants read each message
4. **Binary Mutex + Counting Semaphores**: Standard approach for producer-consumer patterns
5. **Platform Compatibility**: Conditional compilation for `semun` union definition

## Future Improvements

- [ ] Add support for multiple message queues per dialogue
- [ ] Implement priority-based message handling
- [ ] Add message acknowledgment protocol
- [ ] Support message filtering by type
- [ ] Performance monitoring and statistics
- [ ] Persistent message logging

## Testing

Run the built-in demo:
```bash
make run-demo
```

This runs a complete workflow:
1. Initializes shared memory
2. Creates a dialogue
3. Lists active messages

## License

This project is licensed under the MIT License - see [LICENSE](LICENSE) file for details.

## Author

Adam Ahmed (Created as classwork project - Now made public for portfolio)

## References

- POSIX System V IPC specification
- Unix Network Programming (Stevens & Rago)
- Linux man pages: shmget(2), semget(2), semop(2), shmctl(2)

## Note

This is an educational project demonstrating OS concepts. For production use, consider:
- Using message queues instead of custom circular buffers
- Implementing proper error recovery mechanisms
- Adding comprehensive logging
- Using standard messaging libraries (0MQ, RabbitMQ, etc.)
