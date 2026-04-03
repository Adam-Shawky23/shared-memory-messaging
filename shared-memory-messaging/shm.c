
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <signal.h>

#include "shm.h"

/* Forward declarations for internal helper functions (not part of the public API) */
int create_dialogue(SharedMemory *shm, int dialogue_id, pid_t creator_pid);
int leave_dialogue(SharedMemory *shm, int dialogue_id, pid_t pid);
int find_dialogue(SharedMemory *shm, int dialogue_id);
int get_participant_index(Dialogue *dlg, pid_t pid);

/* IPC resource keys are now defined in shm.h for consistency */
/* Note: Keys imported from shm.h */

/* ============================================================================
   SEMAPHORE OPERATIONS
   ============================================================================ */

/**
 * semun union - required for semctl operations
 * Note: macOS defines this, but Linux does not
 */
#if defined(__linux__)
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};
#endif

/**
 * Initialize a semaphore with the given value
 * @param semid: Semaphore ID
 * @param value: Initial semaphore value
 */
void init_semaphore(int semid, int value) {
    union semun arg;
    arg.val = value;
    if (semctl(semid, 0, SETVAL, arg) == -1) {
        perror("semctl init failed");
        exit(1);
    }
}

/**
 * Semaphore down operation (P operation) - decrements semaphore
 * Blocks if semaphore is 0
 */
void sem_down(int semid) {
    struct sembuf sb = {0, -1, 0};
    if (semop(semid, &sb, 1) == -1) {
        perror("sem_down failed");
        exit(1);
    }
}

/**
 * Semaphore up operation (V operation) - increments semaphore
 * Wakes up any waiting processes
 */
void sem_up(int semid) {
    struct sembuf sb = {0, 1, 0};
    if (semop(semid, &sb, 1) == -1) {
        perror("sem_up failed");
        exit(1);
    }
}

/**
 * Diagnostic helper: Report information about existing shared memory segment
 * with the given key. Useful for troubleshooting EINVAL errors during creation
 * (common cause: segment exists with different size).
 */
void report_existing_shm(key_t key) {
    int existing_shmid = shmget(key, 0, 0666); /* search without creating */
    if (existing_shmid == -1) {
        fprintf(stderr, "No existing segment found for key=%d (shmget returned -1, errno=%d: %s)\n",
                key, errno, strerror(errno));
        return;
    }

    struct shmid_ds ds;
    if (shmctl(existing_shmid, IPC_STAT, &ds) == -1) {
        fprintf(stderr, "shmctl(IPC_STAT) failed for shmid=%d: errno=%d (%s)\n",
                existing_shmid, errno, strerror(errno));
        return;
    }

    printf("Existing shared memory segment found:\n");
    printf("  shmid: %d\n", existing_shmid);
    printf("  key: %d\n", key);
    printf("  size: %zu bytes\n", (size_t) ds.shm_segsz);
    printf("  nattch (attached processes): %ld\n", (long) ds.shm_nattch);
    printf("  ctime: %s", ctime(&ds.shm_ctime));
    printf("  To remove this segment (if safe): ipcrm -m %d\n", existing_shmid);
}
/* ============================================================================
   SHARED MEMORY INITIALIZATION
   ============================================================================ */

/**
 * Initialize the shared memory structure with default values
 * Sets up all messages and dialogues as inactive, initializes semaphores
 */
void init_shared_memory(SharedMemory *shm) {
    printf("Initializing shared memory...\n");
    
    /* Initialize all messages as inactive */
    for (int i = 0; i < MAX_MESSAGES; i++) {
        shm->messages[i].is_active = 0;
        shm->messages[i].dialogue_id = -1;
    }
    
    /* Initialize all dialogues as inactive */
    for (int i = 0; i < MAX_DIALOGUES; i++) {
        shm->dialogues[i].is_active = 0;
        shm->dialogues[i].dialogue_id = -1;
        shm->dialogues[i].num_participants = 0;
        shm->dialogues[i].next_sequence_number = 0;
    }
    
    shm->message_write_index = 0;
    shm->num_active_dialogues = 0;
    shm->active_reader_count = 0;
    
    /* Create semaphores for synchronization */
    shm->mutex_semid = semget(SEM_KEY, 1, IPC_CREAT | 0666);
    if (shm->mutex_semid == -1) {
        perror("semget mutex failed");
        exit(1);
    }
    init_semaphore(shm->mutex_semid, 1); /* Binary semaphore (mutex) */
    
    shm->msg_available_semid = semget(SEM_KEY + 1, 1, IPC_CREAT | 0666);
    if (shm->msg_available_semid == -1) {
        perror("semget msg_available failed");
        exit(1);
    }
    init_semaphore(shm->msg_available_semid, 0); /* Starts at 0 */

    /**
     * Counting semaphore for available free slots in the circular buffer.
     * Initialized to MAX_MESSAGES (all slots free). Senders will sem_down
     * before writing; readers will sem_up when releasing a slot.
     */
    shm->slot_available_semid = semget(SEM_KEY + 2, 1, IPC_CREAT | 0666);
    if (shm->slot_available_semid == -1) {
        perror("semget slot_available failed");
        exit(1);
    }
    init_semaphore(shm->slot_available_semid, MAX_MESSAGES);
    
    printf("Shared memory initialized successfully!\n");
    printf("  Mutex semaphore ID: %d\n", shm->mutex_semid);
    printf("  Message available semaphore ID: %d\n", shm->msg_available_semid);
    printf("  Slot-available semaphore ID: %d\n", shm->slot_available_semid);
}

/* ============================================================================
   PUBLIC API - DIALOGUE AND MESSAGE MANAGEMENT
   ============================================================================ */

/**
 * Initialize (create) a new dialogue with the given creator
 * @return Index of created dialogue, or -1 if dialogue creation failed
 */
int initialize_dialogue(SharedMemory *shm, int dialogue_id, pid_t creator_pid) {
    sem_down(shm->mutex_semid);
    int idx = create_dialogue(shm, dialogue_id, creator_pid);
    sem_up(shm->mutex_semid);
    return idx;
}

/**
 * Mark a message as read by the participant at index pidx
 * If all expected readers have read the message, release the message slot
 */
void mark_message_read(SharedMemory *shm, Message *msg, Dialogue *dlg __attribute__((unused)), int pidx) {
    unsigned int mask = (1u << pidx);
    msg->read_bitmap |= mask;
    int readers_seen = __builtin_popcount(msg->read_bitmap);
    if (readers_seen >= msg->total_readers_expected) {
        cleanup_message(shm, msg);
    }
}

/**
 * Clean up a message slot and signal that a slot is available
 */
void cleanup_message(SharedMemory *shm, Message *msg) {
    msg->is_active = 0;
    msg->content[0] = '\0';
    sem_up(shm->slot_available_semid);
}

/**
 * Handle TERMINATE command: remove pid from dialogue, wait for readers to finish,
 * and clean up IPC resources if no readers remain.
 * @return 1 if cleanup was performed, 0 otherwise
 */
int terminate_dialogue(SharedMemory *shm, int dialogue_id, pid_t pid) {
    leave_dialogue(shm, dialogue_id, pid);
    int readers_left = 0;
    int waited = 0;
    while (waited < 20) { /* ~2 seconds */
        sem_down(shm->mutex_semid);
        readers_left = shm->active_reader_count;
        sem_up(shm->mutex_semid);
        if (readers_left == 0) break;
        usleep(100000);
        waited++;
    }
    if (readers_left == 0) {
        semctl(shm->mutex_semid, 0, IPC_RMID);
        semctl(shm->msg_available_semid, 0, IPC_RMID);
        semctl(shm->slot_available_semid, 0, IPC_RMID);
        return 1;
    }
    return 0;
}

/**
 * Send (broadcast) a message to a dialogue
 * @return 0 on success, -1 on failure
 *
 * Special handling: If message content equals "TERMINATE", this triggers
 * cleanup logic and may cause IPC resource removal. The caller should
 * handle process exit appropriately.
 */
int send_message(SharedMemory *shm, int dlg_id, const char *message, pid_t sender_pid) {
    /* Reserve a free message slot */
    sem_down(shm->slot_available_semid);
    sem_down(shm->mutex_semid);

    int dlg_idx = find_dialogue(shm, dlg_id);
    if (dlg_idx == -1) {
        sem_up(shm->mutex_semid);
        sem_up(shm->slot_available_semid);
        return -1;
    }
    Dialogue *dlg = &shm->dialogues[dlg_idx];

    int start = shm->message_write_index;
    int msg_idx = -1;
    for (int k = 0; k < MAX_MESSAGES; k++) {
        int cand = (start + k) % MAX_MESSAGES;
        if (!shm->messages[cand].is_active) { msg_idx = cand; break; }
    }
    if (msg_idx == -1) msg_idx = start;
    Message *msg = &shm->messages[msg_idx];

    msg->dialogue_id = dlg_id;
    msg->sender_pid = sender_pid;
    strncpy(msg->content, message, MAX_CONTENT_LENGTH - 1);
    msg->content[MAX_CONTENT_LENGTH - 1] = '\0';
    msg->sequence_number = dlg->next_sequence_number++;
    msg->timestamp = time(NULL);
    msg->read_bitmap = 0;
    msg->total_readers_expected = dlg->num_participants;
    msg->is_active = 1;

    shm->message_write_index = (msg_idx + 1) % MAX_MESSAGES;

    /* IMPORTANT: Copy message content to local buffer BEFORE releasing mutex.
       Otherwise another process could overwrite this slot between sem_up and strcmp. */
    char msg_content_copy[MAX_CONTENT_LENGTH];
    strncpy(msg_content_copy, msg->content, MAX_CONTENT_LENGTH - 1);
    msg_content_copy[MAX_CONTENT_LENGTH - 1] = '\0';

    sem_up(shm->mutex_semid);
    sem_up(shm->msg_available_semid);

    if (strcmp(msg_content_copy, "TERMINATE") == 0) {
        int cleaned = terminate_dialogue(shm, dlg_id, sender_pid);
        if (cleaned) {
            /* If we removed IPC, caller is responsible for shmem detach */
        }
    }
    return 0;
}

/**
 * Message receiving loop - run this to subscribe to all active dialogues
 * and receive messages. Automatically joins dialogues not yet joined.
 */
void receive_messages(SharedMemory *shm, pid_t my_pid) {
    sem_down(shm->mutex_semid);
    
    /**
     * Auto-join any active dialogues this process hasn't already joined.
     * This allows recv to work even when run in a different process than create/join.
     */
    for (int i = 0; i < MAX_DIALOGUES; i++) {
        if (shm->dialogues[i].is_active) {
            Dialogue *dlg = &shm->dialogues[i];
            /* Check if already a participant */
            int already_participant = 0;
            for (int j = 0; j < dlg->num_participants; j++) {
                if (dlg->participant_pids[j] == my_pid) {
                    already_participant = 1;
                    break;
                }
            }
            /* If not already a participant and space available, join */
            if (!already_participant && dlg->num_participants < MAX_PARTICIPANTS) {
                dlg->participant_pids[dlg->num_participants] = my_pid;
                dlg->num_participants++;
                printf("Auto-joined dialogue %d (PID %d)\n", dlg->dialogue_id, my_pid);
            }
        }
    }
    
    shm->active_reader_count++;
    sem_up(shm->mutex_semid);

    printf("Entering reader mode (PID %d). Waiting for messages...\n", my_pid);
    int terminate_flag = 0;
    int terminate_dialogue = -1;
    while (!terminate_flag) {
        int handled = 0;
        sem_down(shm->mutex_semid);
        for (int i = 0; i < MAX_MESSAGES; i++) {
            Message *msg = &shm->messages[i];
            if (!msg->is_active) continue;

            int dlg_idx = find_dialogue(shm, msg->dialogue_id);
            if (dlg_idx == -1) continue;
            Dialogue *dlg = &shm->dialogues[dlg_idx];
            int pidx = get_participant_index(dlg, my_pid);
            if (pidx == -1) continue;

            unsigned int mask = (1u << pidx);
            if (msg->read_bitmap & mask) continue;

            /* Mark as read */
            msg->read_bitmap |= mask;
            handled = 1;

            printf("\n--- Received message for dialogue %d ---\n", msg->dialogue_id);
            printf("  From PID: %d\n", msg->sender_pid);
            printf("  Sequence: %d\n", msg->sequence_number);
            printf("  Content: %s\n", msg->content);
            printf("  Timestamp: %s", ctime(&msg->timestamp));

            if (strcmp(msg->content, "TERMINATE") == 0) {
                printf("TERMINATE message received. Exiting reader.\n");
                terminate_flag = 1;
                terminate_dialogue = msg->dialogue_id;
                sem_up(shm->mutex_semid);
                i = MAX_MESSAGES;
                break;
            }

            int readers_seen = __builtin_popcount(msg->read_bitmap);
            if (readers_seen >= msg->total_readers_expected) {
                msg->is_active = 0;
                msg->content[0] = '\0';
                sem_up(shm->slot_available_semid);
            }
        }
        if (!terminate_flag) sem_up(shm->mutex_semid);

        if (!handled) {
            struct sembuf sb = {0, -1, IPC_NOWAIT};
            semop(shm->msg_available_semid, &sb, 1); /* ignore errors */
            usleep(200000);
        }
    }

    if (terminate_flag && terminate_dialogue != -1) {
        leave_dialogue(shm, terminate_dialogue, my_pid);
        sem_down(shm->mutex_semid);
        shm->active_reader_count--;
        int active_readers_left = shm->active_reader_count;
        sem_up(shm->mutex_semid);

        if (active_readers_left == 0) {
            semctl(shm->mutex_semid, 0, IPC_RMID);
            semctl(shm->msg_available_semid, 0, IPC_RMID);
            semctl(shm->slot_available_semid, 0, IPC_RMID);
            /* caller must detach and remove shm */
            printf("TERMINATE received: cleaned up resources and exiting.\n");
            return;
        } else {
            printf("TERMINATE received: left dialogue %d and exiting reader.\n", terminate_dialogue);
            return;
        }
    }
}

/* ============================================================================
   INTERNAL HELPER FUNCTIONS
   ============================================================================ */

/**
 * Find a dialogue by ID
 * @return Index of dialogue or -1 if not found
 */
int find_dialogue(SharedMemory *shm, int dialogue_id) {
    for (int i = 0; i < MAX_DIALOGUES; i++) {
        if (shm->dialogues[i].is_active &&
            shm->dialogues[i].dialogue_id == dialogue_id) {
            return i;
        }
    }
    return -1;
}

/**
 * Get the index of a participant within a dialogue
 * @return Index of participant or -1 if not found
 */
int get_participant_index(Dialogue *dlg, pid_t pid) {
    for (int i = 0; i < dlg->num_participants; i++) {
        if (dlg->participant_pids[i] == pid) return i;
    }
    return -1;
}

/**
 * Create a new dialogue with the given ID and creator
 * @return Index of created dialogue or -1 if no space available
 */
int create_dialogue(SharedMemory *shm, int dialogue_id, pid_t creator_pid) {
    for (int i = 0; i < MAX_DIALOGUES; i++) {
        if (!shm->dialogues[i].is_active) {
            shm->dialogues[i].dialogue_id = dialogue_id;
            shm->dialogues[i].is_active = 1;
            shm->dialogues[i].num_participants = 1;
            shm->dialogues[i].participant_pids[0] = creator_pid;
            shm->dialogues[i].next_sequence_number = 0;
            shm->num_active_dialogues++;
            return i;
        }
    }
    return -1; /* No space */
}

/**
 * Remove a participant from a dialogue. If dialogue becomes empty,
 * mark it as inactive and release all its messages (making slots available).
 *
 * @return 1 if this was the last participant and dialogue was freed, 0 otherwise
 *
 * Note: This function acquires the mutex internally.
 */
int leave_dialogue(SharedMemory *shm, int dialogue_id, pid_t pid) {
    sem_down(shm->mutex_semid);
    int idx = find_dialogue(shm, dialogue_id);
    if (idx == -1) {
        sem_up(shm->mutex_semid);
        return 0;
    }

    Dialogue *dlg = &shm->dialogues[idx];
    /* Remove pid from participant_pids */
    int found = 0;
    for (int i = 0; i < dlg->num_participants; i++) {
        if (dlg->participant_pids[i] == pid) {
            found = 1;
            /* Shift remaining participants */
            for (int j = i; j < dlg->num_participants - 1; j++) {
                dlg->participant_pids[j] = dlg->participant_pids[j+1];
            }
            dlg->num_participants--;
            break;
        }
    }

    if (!found) {
        sem_up(shm->mutex_semid);
        return 0;
    }

    /**
     * After removing the requested PID, remove any dead (non-existent) participant PIDs.
     * This prevents old creators (who terminated after create) from blocking cleanup.
     */
    for (int i = 0; i < dlg->num_participants; /* incremented inside */) {
        pid_t p = dlg->participant_pids[i];
        if (kill(p, 0) == -1 && errno == ESRCH) {
            /* Remove dead PID */
            for (int j = i; j < dlg->num_participants - 1; j++) {
                dlg->participant_pids[j] = dlg->participant_pids[j+1];
            }
            dlg->num_participants--;
            /* Don't increment i, check new resident at this index */
            continue;
        }
        i++;
    }

    /* If dialogue has no remaining participants, free it and its messages */
    int dialogue_was_freed = 0;
    if (dlg->num_participants == 0) {
        dlg->is_active = 0;
        dlg->dialogue_id = -1;
        shm->num_active_dialogues--;
        dialogue_was_freed = 1;

        /* Release any messages belonging to this dialogue */
        for (int i = 0; i < MAX_MESSAGES; i++) {
            Message *m = &shm->messages[i];
            if (m->is_active && m->dialogue_id == dialogue_id) {
                m->is_active = 0;
                m->content[0] = '\0';
                /* Signal freed slot */
                sem_up(shm->slot_available_semid);
            }
        }
    }

    sem_up(shm->mutex_semid);
    return dialogue_was_freed;
}

/**
 * Join an existing dialogue
 * @return Index of dialogue on success, -1 on failure (dialogue not found or full)
 *
 * NOTE: This function must be called without holding the mutex.
 * It acquires the mutex internally for thread-safe access.
 */
int join_dialogue(SharedMemory *shm, int dialogue_id, pid_t joiner_pid) {
    sem_down(shm->mutex_semid);
    
    int idx = find_dialogue(shm, dialogue_id);
    if (idx == -1) {
        sem_up(shm->mutex_semid);
        return -1;
    }
    
    Dialogue *dlg = &shm->dialogues[idx];
    if (dlg->num_participants >= MAX_PARTICIPANTS) {
        sem_up(shm->mutex_semid);
        return -1;
    }
    
    /* Check if already a participant */
    for (int i = 0; i < dlg->num_participants; i++) {
        if (dlg->participant_pids[i] == joiner_pid) {
            sem_up(shm->mutex_semid);
            return idx; /* Already participating */
        }
    }
    
    dlg->participant_pids[dlg->num_participants] = joiner_pid;
    dlg->num_participants++;
    
    sem_up(shm->mutex_semid);
    return idx;
}

/**
 * List all active dialogues (read-only, non-blocking)
 * Provides a snapshot of current system state without attaching as participant
 */
void list_dialogues(SharedMemory *shm) {
    printf("\n=== Active Dialogues ===\n");
    sem_down(shm->mutex_semid);
    
    int found = 0;
    for (int i = 0; i < MAX_DIALOGUES; i++) {
        if (shm->dialogues[i].is_active) {
            Dialogue *dlg = &shm->dialogues[i];
            printf("ID: %d  |  Participants: %d  |  Messages Sent: %d\n",
                   dlg->dialogue_id, dlg->num_participants, dlg->next_sequence_number);
            found = 1;
        }
    }
    
    if (!found) {
        printf("(No active dialogues)\n");
    }
    
    sem_up(shm->mutex_semid);
}

/**
 * Display comprehensive system status
 * Shows current state of shared memory, semaphores, and buffers
 */
void print_system_status(SharedMemory *shm) {
    printf("\n=== Shared Memory Status ===\n");
    sem_down(shm->mutex_semid);
    
    /* Count active messages and semaphore values */
    int active_msgs = 0;
    for (int i = 0; i < MAX_MESSAGES; i++) {
        if (shm->messages[i].is_active) active_msgs++;
    }
    
    int sem_mutex_val = semctl(shm->mutex_semid, 0, GETVAL);
    int sem_msg_val = semctl(shm->msg_available_semid, 0, GETVAL);
    int sem_slots_val = semctl(shm->slot_available_semid, 0, GETVAL);
    
    printf("Dialogues:       %d/%d active\n", shm->num_active_dialogues, MAX_DIALOGUES);
    printf("Message Buffer:  %d/%d slots used\n", active_msgs, MAX_MESSAGES);
    printf("Active Readers:  %d\n", shm->active_reader_count);
    printf("\nSemaphore Values:\n");
    printf("  mutex:         %d (1=available, 0=locked)\n", sem_mutex_val);
    printf("  msg_available: %d (messages ready to read)\n", sem_msg_val);
    printf("  slot_available:%d (free buffer slots)\n", sem_slots_val);
    
    printf("\n--- Active Dialogues ---\n");
    for (int i = 0; i < MAX_DIALOGUES; i++) {
        if (shm->dialogues[i].is_active) {
            Dialogue *dlg = &shm->dialogues[i];
            printf("[%d] ID=%d  parts=%d  seq=%d\n",
                   i, dlg->dialogue_id, dlg->num_participants, dlg->next_sequence_number);
        }
    }
    
    sem_up(shm->mutex_semid);
}

/**
 * Display information about a specific dialogue
 */
void display_dialogue(SharedMemory *shm, int dialogue_id) {
    int idx = find_dialogue(shm, dialogue_id);
    if (idx == -1) {
        printf("Dialogue %d not found.\n", dialogue_id);
        return;
    }
    
    Dialogue *dlg = &shm->dialogues[idx];
    printf("\n=== Dialogue %d ===\n", dlg->dialogue_id);
    printf("Status: %s\n", dlg->is_active ? "Active" : "Terminated");
    printf("Participants: %d\n", dlg->num_participants);
    printf("PIDs: ");
    for (int i = 0; i < dlg->num_participants; i++) {
        printf("%d ", dlg->participant_pids[i]);
    }
    printf("\nNext sequence number: %d\n", dlg->next_sequence_number);
}

/* ============================================================================
   MAIN PROGRAM
   ============================================================================ */

/**
 * Test program demonstrating the shared memory messaging system
 * Supports commands: init, create, join, send, recv
 */
int main(int argc, char *argv[]) {
    int shmid;
    SharedMemory *shm;
    
    if (argc < 2) {
        printf("Usage:\n");
        printf("  %s init                       - Initialize shared memory\n", argv[0]);
        printf("  %s create <dlg_id>            - Create dialogue\n", argv[0]);
        printf("  %s join <dlg_id>              - Join dialogue\n", argv[0]);
        printf("  %s send <dlg_id> \"<msg>\"      - Send message (use quotes)\n", argv[0]);
        printf("  %s recv                       - Start reader (receive messages)\n", argv[0]);
        printf("  %s list                       - List active dialogues\n", argv[0]);
        printf("  %s status                     - Show system status\n", argv[0]);
        exit(1);
    }
    
    /* Get or create shared memory segment */
    printf("Requesting shared memory segment with key=%d, size=%zu bytes\n", SHM_KEY, sizeof(SharedMemory));
    shmid = shmget(SHM_KEY, sizeof(SharedMemory), IPC_CREAT | 0666);
    if (shmid == -1) {
        fprintf(stderr, "shmget failed: errno=%d (%s)\n", errno, strerror(errno));
        perror("shmget failed");
        /* Provide extra diagnostics: check for existing segment with this key */
        report_existing_shm(SHM_KEY);
        exit(1);
    }
    
    /* Attach to shared memory */
    shm = (SharedMemory *) shmat(shmid, NULL, 0);
    if (shm == (SharedMemory *) -1) {
        perror("shmat failed");
        exit(1);
    }
    
    pid_t my_pid = getpid();
    
    /* Handle commands */
    if (strcmp(argv[1], "init") == 0) {
        init_shared_memory(shm);
        printf("Shared memory initialized at address: %p\n", (void *)shm);
        
    } else if (strcmp(argv[1], "create") == 0) {
        if (argc < 3) {
            printf("Usage: %s create <dialogue_id>\n", argv[0]);
            exit(1);
        }
        int dlg_id = atoi(argv[2]);
        sem_down(shm->mutex_semid);
        int idx = create_dialogue(shm, dlg_id, my_pid);
        sem_up(shm->mutex_semid);
        
        if (idx != -1) {
            printf("Dialogue %d created by PID %d\n", dlg_id, my_pid);
            display_dialogue(shm, dlg_id);
        } else {
            printf("Failed to create dialogue (may already exist or no space)\n");
        }
        
    } else if (strcmp(argv[1], "join") == 0) {
        if (argc < 3) {
            printf("Usage: %s join <dialogue_id>\n", argv[0]);
            exit(1);
        }
        int dlg_id = atoi(argv[2]);
        /* join_dialogue now handles its own mutex protection */
        int idx = join_dialogue(shm, dlg_id, my_pid);
        
        if (idx != -1) {
            printf("PID %d joined dialogue %d\n", my_pid, dlg_id);
            display_dialogue(shm, dlg_id);
        } else {
            printf("Failed to join dialogue (doesn't exist or full)\n");
        }
        
    } else if (strcmp(argv[1], "send") == 0) {
        if (argc < 4) {
            printf("Usage: %s send <dialogue_id> <message>\n", argv[0]);
            exit(1);
        }
        int dlg_id = atoi(argv[2]);
        int res = send_message(shm, dlg_id, argv[3], my_pid);
        if (res == -1) {
            printf("Dialogue %d not found or send failed\n", dlg_id);
            shmdt(shm);
            exit(1);
        }
        if (strcmp(argv[3], "TERMINATE") == 0) {
            int check = semctl(shm->mutex_semid, 0, GETVAL);
            if (check == -1 && errno == EINVAL) {
                shmdt(shm);
                shmctl(shmid, IPC_RMID, NULL);
                printf("TERMINATE sent: cleaned up resources and exiting.\n");
                exit(0);
            } else {
                printf("TERMINATE sent: exiting sender process (readers may still be active).\n");
                shmdt(shm);
                exit(0);
            }
        }
        
    } else if (strcmp(argv[1], "recv") == 0 || strcmp(argv[1], "reader") == 0) {
        /**
         * Reader loop: join dialogues already participated in and
         * consume messages exactly once using read_bitmap.
         * This loop polls for messages and sleeps briefly when none found.
         */
        receive_messages(shm, my_pid);

    } else if (strcmp(argv[1], "list") == 0) {
        /* List all active dialogues without attaching as participant */
        list_dialogues(shm);
        
    } else if (strcmp(argv[1], "status") == 0) {
        /* Display comprehensive system status */
        print_system_status(shm);

    } else {
        printf("Unknown command: %s\n", argv[1]);
    }
    
    /* Detach from shared memory */
    shmdt(shm);
    
    return 0;
}