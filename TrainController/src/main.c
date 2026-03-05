#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/neutrino.h>
#include <sys/wait.h>
#include <pthread.h>
#include "ipc_protocol.h"

#define CONFIG_LINE_MAX 256

// Global train storage - TrainController is the authoritative source
train_data_t train_list[MAX_TRAINS];
int train_count = 0;

// Mutex protecting train data
pthread_mutex_t train_mutex = PTHREAD_MUTEX_INITIALIZER;

// TODO change this
#define DEADLOCK_MANAGER_PID 1234   // Replace with actual PID
#define DEADLOCK_MANAGER_CHID 1     // Replace with actual CHID

// IPC server function to handle queries from DeadlockManager
void *ipc_server_thread(void *arg) {
    int chid = *(int *)arg;
    int rcvid;
    train_query_message_t msg;
    train_query_message_t reply;

    printf("TrainController IPC server started (CHID: %d)\n", chid);

    while (1) {
        // Wait for incoming messages
        rcvid = MsgReceive(chid, &msg, sizeof(msg), NULL);
        if (rcvid == -1) {
            continue;
        }

        // Default reply
        reply.type = MSG_DENY;
        reply.train_id = msg.train_id;

        // Lock train data for processing
        pthread_mutex_lock(&train_mutex);

        switch (msg.type) {
            case MSG_GET_TRAIN_DATA:
                // Find train data
                for (int i = 0; i < train_count; i++) {
                    if (train_list[i].train_id == msg.train_id) {
                        reply.type = MSG_TRAIN_DATA_REPLY;
                        reply.train_data = train_list[i];
                        break;
                    }
                }
                break;

            default:
                printf("TrainController: Unknown message type %d from train %d\n", 
                       msg.type, msg.train_id);
                break;
        }

        // Unlock train data
        pthread_mutex_unlock(&train_mutex);

        // Send reply
        MsgReply(rcvid, 0, &reply, sizeof(reply));
    }

    return NULL;
}

// Function to run the train process
void run_train_process(train_data_t train) {
    printf("Train %d starting at track %d heading to %d\n", train.train_id, train.track_id, train.destination);

    // Connect to DeadlockManager
    int coid = ConnectAttach(0, DEADLOCK_MANAGER_PID, DEADLOCK_MANAGER_CHID, 0, 0);
    if (coid == -1) {
        perror("ConnectAttach failed");
        exit(1);
    }

    // TODO make this a loop based on tick formulation until the train reaches its destination in which we can kill the process for the train

    // TODO this would be based on destination and starting track
    // Example: Request starting track
    ipc_message_t msg;
    msg.type = MSG_REQUEST_TRACK;
    msg.train_id = train.train_id;
    msg.track_id = train.track_id;

    ipc_message_t reply;

    // Check reply
    if (MsgSend(coid, &msg, sizeof(msg), &reply, sizeof(reply)) == -1) {
        perror("MsgSend failed");
        exit(1);
    }

    // Print result of track request
    if (reply.type == MSG_ACK) {
        printf("Train %d acquired track %d\n", train.train_id, train.track_id);
    } else {
        printf("Train %d denied track %d\n", train.train_id, train.track_id);
    }

    // Infinite movement loop would go here
    // TODO Replace with tick formulation
    while (1) {
        sleep(1);
    }
}

// Main function to read config and spawn train processes
// Usage: ./train_controller <train_config_file>
int main(int argc, char *argv[]) {
    // Ensure valid input
    if (argc != 2) {
        printf("Usage: %s <train_config_file>\n", argv[0]);
        return 1;
    }

    // Create IPC channel for DeadlockManager queries
    int chid = ChannelCreate(0);
    if (chid == -1) {
        perror("ChannelCreate failed");
        return 1;
    }

    // Start IPC server thread
    pthread_t server_thread;
    if (pthread_create(&server_thread, NULL, ipc_server_thread, &chid) != 0) {
        perror("Failed to create IPC server thread");
        return 1;
    }

    printf("TrainController starting... (PID: %d, CHID: %d)\n", getpid(), chid);

    // Open config file
    FILE *file = fopen(argv[1], "r");
    if (!file) {
        perror("Failed to open config file");
        return 1;
    }

    // Lock train data for modification
    pthread_mutex_lock(&train_mutex);

    // Loop through config lines
    char line[CONFIG_LINE_MAX];
    while (fgets(line, sizeof(line), file)) {
        // Skip comments and empty lines
        if (line[0] == '#' || strlen(line) < 3)
            continue;

        // Parse train data
        train_data_t train;
        memset(&train, 0, sizeof(train_data_t));
        if (sscanf(line, "%d %d %d %d %d", &train.train_id, &train.track_id, &train.destination, &train.speed, &train.length) != 5) {
            continue;
        }

        // Store train data globally
        if (train_count < MAX_TRAINS) {
            train_list[train_count] = train;
            train_count++;
            printf("Loaded train %d: track %d -> %d, speed %d mm/s, length %d mm\n",
                   train.train_id, train.track_id, train.destination, train.speed, train.length);
        } else {
            printf("Warning: Maximum train count reached, skipping train %d\n", train.train_id);
        }

        // Spawn a process for each train
        pid_t pid = fork();
        if (pid == 0) {
            // CHILD
            run_train_process(train);
            exit(0);
        } else if (pid < 0) {
            perror("fork failed");
        } else {
            // PARENT
            printf("Spawned Train %d (PID %d)\n", train.train_id, pid);
        }
    }
    fclose(file);

    // Unlock train data
    pthread_mutex_unlock(&train_mutex);

    // Wait for all child processes
    while (wait(NULL) > 0);

    // Cleanup
    ChannelDestroy(chid);
    return 0;
}

    // TODO Parent waits for children (optional)
    while (wait(NULL) > 0);
    return 0;
}