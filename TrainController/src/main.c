#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/neutrino.h>
#include <sys/wait.h>
#include <sys/dispatch.h>
#include <pthread.h>
#include "ipc_protocol.h"

#define TRAIN_CONTROLLER_NAME "TrainController"
#define DEADLOCK_MANAGER_NAME "DeadlockManager"

#define CONFIG_LINE_MAX 256

// Global train storage - TrainController is the authoritative source
train_data_t train_list[MAX_TRAINS];
int train_count = 0;

// Mutex protecting train data
pthread_mutex_t train_mutex = PTHREAD_MUTEX_INITIALIZER;

// IPC server function to handle queries from DeadlockManager
void *ipc_server_thread(void *arg) {
    int chid = *(int *)arg;
    int rcvid;
    union {
        struct _pulse pulse;
        train_query_message_t msg;
    } recv;
    train_query_message_t reply;

    printf("TrainController IPC server started (CHID: %d)\n", chid);

    while (1) {
        // Wait for incoming messages
        rcvid = MsgReceive(chid, &recv, sizeof(recv), NULL);
        if (rcvid == -1) {
            continue;
        }

        // Pulses are not application messages; ignore/handle them explicitly.
        if (rcvid == 0) {
            if (recv.pulse.code == _PULSE_CODE_DISCONNECT) {
                ConnectDetach(recv.pulse.scoid);
            }
            continue;
        }

        train_query_message_t msg = recv.msg;

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

    // Create a channel for receiving position updates
    int chid = ChannelCreate(0);
    if (chid == -1) {
        perror("ChannelCreate failed");
        exit(1);
    }

    // Attach a name for the train so DeadlockManager can find it
    char train_name[32];
    sprintf(train_name, "Train%d", train.train_id);
    name_attach_t *attach = name_attach(NULL, train_name, 0);
    if (attach == NULL) {
        perror("name_attach failed");
        exit(1);
    }

    // Connect to DeadlockManager by name
    int dm_coid = name_open(DEADLOCK_MANAGER_NAME, 0);
    if (dm_coid == -1) {
        printf("Train %d: Failed to connect to DeadlockManager: %s\n", train.train_id, strerror(errno));
        exit(1);
    }

    // Current state
    int current_track = train.track_id;
    int destination_track = train.destination;
    int current_track_length = 0;
    double current_front_pos = 0.0;
    double current_rear_pos = 0.0;
    double current_speed = 0.0;
    bool has_requested_destination = false;

    // Request starting track
    ipc_message_t msg;
    msg.type = MSG_REQUEST_TRACK;
    msg.train_id = train.train_id;
    msg.track_id = current_track;

    ipc_message_t reply;

    if (MsgSend(dm_coid, &msg, sizeof(msg), &reply, sizeof(reply)) == -1) {
        perror("MsgSend failed");
        exit(1);
    }

    if (reply.type != MSG_ACK) {
        printf("Train %d denied initial track %d\n", train.train_id, current_track);
        exit(1);
    }

    printf("Train %d acquired initial track %d\n", train.train_id, current_track);

    // Tick-based main loop
    time_t last_tick = time(NULL);
    while (1) {
        time_t current_time = time(NULL);

        // Tick every 10ms
        if (difftime(current_time, last_tick) >= 0.01) {
            // Check for position updates (non-blocking)
            position_update_message_t update_msg;
            struct timespec timeout = {0, 0};  // Non-blocking
            int rcvid = MsgReceive(chid, &update_msg, sizeof(update_msg), &timeout);

            if (rcvid != -1) {
                // Received position update
                if (update_msg.type == MSG_POSITION_UPDATE && update_msg.train_id == train.train_id) {
                    current_track = update_msg.track_id;
                    current_track_length = update_msg.track_length;
                    current_front_pos = update_msg.front_position;
                    current_rear_pos = update_msg.rear_position;
                    current_speed = update_msg.current_speed;

                    printf("Train %d: Tick %ld - Track %d (len=%dmm), Front=%.0fmm, Rear=%.0fmm, Speed=%.0fmm/s\n",
                           train.train_id, update_msg.tick_time, current_track, current_track_length,
                           current_front_pos, current_rear_pos, current_speed);
                }

                // Reply to the message
                MsgReply(rcvid, 0, NULL, 0);
            }

            // Decision making on each tick
            // If approaching end of track and haven't requested destination, request it
            if (current_track_length > 0 && current_front_pos > (double)current_track_length * 0.8 && !has_requested_destination && current_track != destination_track) {
                printf("Train %d requesting destination track %d\n", train.train_id, destination_track);

                msg.type = MSG_REQUEST_TRACK;
                msg.train_id = train.train_id;
                msg.track_id = destination_track;

                if (MsgSend(dm_coid, &msg, sizeof(msg), &reply, sizeof(reply)) == -1) {
                    perror("Failed to request destination track");
                } else if (reply.type == MSG_ACK) {
                    printf("Train %d acquired destination track %d\n", train.train_id, destination_track);
                    has_requested_destination = true;
                } else {
                    printf("Train %d denied destination track %d\n", train.train_id, destination_track);
                }
            }

            // If reached destination, exit
            if (current_track == destination_track && current_track_length > 0 && current_front_pos > (double)current_track_length * 0.9) {
                printf("Train %d reached destination track %d\n", train.train_id, destination_track);
                break;
            }

            last_tick = current_time;
        }

        // Small sleep to prevent busy waiting
        usleep(1000);  // 1ms
    }

    // Cleanup
    name_detach(attach, 0);
    ChannelDestroy(chid);
    ConnectDetach(dm_coid);
}

// Main function to read config and spawn train processes
// Usage: ./train_controller <train_config_file>
int main(int argc, char *argv[]) {
    // Ensure valid input
    if (argc != 2) {
        printf("Usage: %s <train_config_file>\n", argv[0]);
        return 1;
    }

    // Attach a name to our channel for discovery
    name_attach_t *attach = name_attach(NULL, TRAIN_CONTROLLER_NAME, 0);
    if (attach == NULL) {
        perror("name_attach failed");
        return 1;
    }
    int chid = attach->chid;

    // Start IPC server thread
    pthread_t server_thread;
    if (pthread_create(&server_thread, NULL, ipc_server_thread, &chid) != 0) {
        perror("Failed to create IPC server thread");
        return 1;
    }

    printf("TrainController starting... (PID: %d, CHID: %d, Name: %s)\n", getpid(), chid, TRAIN_CONTROLLER_NAME);

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
    name_detach(attach, 0);
    return 0;
}