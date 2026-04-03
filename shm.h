#ifndef SHM_H
#define SHM_H

#include <sys/types.h>
#include <time.h>
#include <assert.h>

/** Configuration constants */
#define MAX_MESSAGES 100
#define MAX_DIALOGUES 10
#define MAX_PARTICIPANTS 20
#define MAX_CONTENT_LENGTH 256

/** IPC resource keys - use collision-resistant values */
#define SHM_KEY  0x4D534700U   /* 'MSG\0' - more collision-resistant */
#define SEM_KEY  0x4D534701U   /* semaphore key base */

/** Compile-time assertion: bitmap must be large enough for MAX_PARTICIPANTS */
_Static_assert(MAX_PARTICIPANTS <= (int)(sizeof(unsigned int) * 8), 
               "MAX_PARTICIPANTS exceeds bitmap capacity (32 bits)");


/** Message structure for the circular buffer */
typedef struct {
    int dialogue_id;
    pid_t sender_pid;
    char content[MAX_CONTENT_LENGTH];
    int sequence_number;
    time_t timestamp;
    int is_active;
    unsigned int read_bitmap;      /* Tracks which participants have read the message */
    int total_readers_expected;    /* Number of participants in the dialogue */
} Message;

/** Dialogue metadata and participant information */
typedef struct {
    int dialogue_id;
    int num_participants;
    pid_t participant_pids[MAX_PARTICIPANTS];
    int is_active;
    int next_sequence_number;      /* Used for ordering messages within dialogue */
} Dialogue;

/** Shared memory layout - contains all messages, dialogues, and synchronization primitives */
typedef struct {
    Message messages[MAX_MESSAGES];
    Dialogue dialogues[MAX_DIALOGUES];
    int message_write_index;
    int num_active_dialogues;
    int active_reader_count;

    /* Semaphore IDs stored in shared memory for convenience across processes */
    int mutex_semid;               /* Binary semaphore for mutual exclusion */
    int msg_available_semid;       /* Counting semaphore for message availability */
    int slot_available_semid;      /* Counting semaphore for free message slots */
} SharedMemory;

/** 
 * Public API function prototypes:
 * - initialize_dialogue: Create a new dialogue with the creator as first participant
 * - join_dialogue: Add a participant to an existing dialogue
 * - send_message: Broadcast a message to all participants in a dialogue
 * - receive_messages: Start the receiver loop for a process
 * - mark_message_read: Mark a message as read by a specific participant
 * - cleanup_message: Release a message slot after all participants have read it
 * - terminate_dialogue: Handle TERMINATE message and clean up IPC resources
 */
int initialize_dialogue(SharedMemory *shm, int dialogue_id, pid_t creator_pid);
int join_dialogue(SharedMemory *shm, int dialogue_id, pid_t joiner_pid);
int send_message(SharedMemory *shm, int dialogue_id, const char *message, pid_t sender_pid);
void receive_messages(SharedMemory *shm, pid_t my_pid);
void mark_message_read(SharedMemory *shm, Message *msg, Dialogue *dlg, int pidx);
void cleanup_message(SharedMemory *shm, Message *msg);
int terminate_dialogue(SharedMemory *shm, int dialogue_id, pid_t pid);

#endif 
