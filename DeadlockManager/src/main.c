#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/neutrino.h>
#include <sys/dispatch.h>

#include "ipc_protocol.h"
#include "resource_manager.h"
#include "physics_engine.h"

#define CONFIG_LINE_MAX 256

// Global track storage
track_data_t track_list[MAX_TRACKS];
int track_count = 0;

// Local storage for active trains (while they're on tracks)
train_data_t active_trains[MAX_TRAINS];
int active_train_count = 0;

// Mutex protecting the resource manager
pthread_mutex_t track_mutex = PTHREAD_MUTEX_INITIALIZER;

// TrainController connection info
// TODO: These need to match the actual TrainController PID and CHID
// In a real system, these would be discovered through a name service or configuration
#define TRAIN_CONTROLLER_PID 5678   // Must match TrainController's actual PID
#define TRAIN_CONTROLLER_CHID 2     // Must match TrainController's actual CHID

// Query train data from TrainController and store locally
train_data_t *get_or_create_train_data(int train_id) {
    // Check if we already have this train's data
    for (int i = 0; i < active_train_count; i++) {
        if (active_trains[i].train_id == train_id) {
            return &active_trains[i];
        }
    }

    // Train not found locally, query from TrainController
    if (active_train_count >= MAX_TRAINS) {
        printf("Maximum active train count reached\n");
        return NULL;
    }

    // Connect to TrainController
    int coid = ConnectAttach(0, TRAIN_CONTROLLER_PID, TRAIN_CONTROLLER_CHID, 0, 0);
    if (coid == -1) {
        printf("Failed to connect to TrainController for train %d\n", train_id);
        return NULL;
    }

    // Send query
    train_query_message_t msg;
    msg.type = MSG_GET_TRAIN_DATA;
    msg.train_id = train_id;

    train_query_message_t reply;

    if (MsgSend(coid, &msg, sizeof(msg), &reply, sizeof(reply)) == -1) {
        printf("Failed to query train data for train %d\n", train_id);
        ConnectDetach(coid);
        return NULL;
    }

    ConnectDetach(coid);

    if (reply.type == MSG_TRAIN_DATA_REPLY) {
        // Store the train data locally
        active_trains[active_train_count] = reply.train_data;
        active_train_count++;
        return &active_trains[active_train_count - 1];
    } else {
        printf("TrainController denied train data request for train %d\n", train_id);
        return NULL;
    }
}

// Load track topology from configuration file
// Format per line:
//     track_id length direction endpoint1 endpoint2 endpoint3 endpoint4
int load_track_data(const char *filename) {
    // Open config file
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Failed to open track file");
        return 0;
    }

    // Loop through config lines
    char line[CONFIG_LINE_MAX];
    while (fgets(line, sizeof(line), file)) {
        // Skip comments or empty lines
        if (line[0] == '#' || strlen(line) < 3)
            continue;

        // Parse track data
        track_data_t track;
        if (sscanf(line, "%d %d %d %d %d %d %d", &track.track_id, &track.length, &track.direction, &track.endpoints[0], &track.endpoints[1], &track.endpoints[2], &track.endpoints[3]) != 7) {
            printf("Invalid track line: %s\n", line);
            continue;
        }

        // Initialize trains on track to NULL (empty)
        memset(track.trains, 0, sizeof(track.trains));
        track.num_trains = 0;

        // Initialize request queue
        init_track_queue(&track);

        if (track.track_id < 0 || track.track_id >= MAX_TRACKS) {
            printf("Invalid track ID: %d\n", track.track_id);
            continue;
        }

        // Add track to list of tracks
        track_list[track.track_id] = track;
        track_count++;
        printf("Loaded track %d\n", track.track_id);
    }

    fclose(file);
    return 1;
}


// Main server loop
int main(int argc, char *argv[]) {
    // Ensure valid input
    if (argc != 2) {
        printf("Usage: %s <track_config_file>\n", argv[0]);
        return 1;
    }

    int chid; // Channel ID
    int rcvid; // Receive ID
    ipc_message_t msg; // Incoming message buffer

    // Load track topology from file
    if (!load_track_data(argv[1])) {
        return 1;
    }

    // Initialize track ownership table
    // TODO this might not be needed
    init_resource_manager();

    // Create communication channel
    chid = ChannelCreate(0);
    if (chid == -1) {
        perror("ChannelCreate failed");
        return 1;
    }

    printf("\nDeadlockManager running...\n");
    printf("PID: %d\n", getpid());
    printf("CHID: %d\n\n", chid);

    // Main server loop
    // TODO make this fun on tick formulation
    // TODO use mutex, cond var, etc
    time_t last_update = time(NULL);
    while (1) {
        // Periodic physics update (every 100ms)
        time_t current_time = time(NULL);
        if (difftime(current_time, last_update) >= 0.1) {  // 100ms
            // Update all tracks with physics
            for (int i = 0; i < track_count; i++) {
                if (track_list[i].track_id >= 0) {
                    track_list[i] = update_track_data(track_list[i]);
                }
            }
            last_update = current_time;
        }

        // Wait for incoming messages from trains
        rcvid = MsgReceive(chid, &msg, sizeof(msg), NULL);
        if (rcvid == -1)
            continue;

        ipc_message_t reply;

        // Default reply is DENY
        reply.type = MSG_DENY;
        reply.train_id = msg.train_id;
        reply.track_id = msg.track_id;

        // Lock track for processing
        pthread_mutex_lock(&track_mutex);

        // Process message based on type
        switch (msg.type) {
            case MSG_REQUEST_TRACK:
                // Get train data from TrainController (or local cache)
                train_data_t *train = get_or_create_train_data(msg.train_id);
                
                if (train == NULL) {
                    reply.type = MSG_DENY;
                    printf("Train %d denied track %d (no train data available)\n", msg.train_id, msg.track_id);
                } else if (request_track(msg.train_id, msg.track_id)) {
                    // Request accepted - add train to track and initialize physics
                    track_data_t *track = &track_list[msg.track_id];

                    // Add train to track's train list
                    if (track->num_trains < MAX_TRAINS) {
                        track->trains[track->num_trains] = train;
                        track->num_trains++;

                        // Initialize train physics on this track
                        train->track_id = msg.track_id;
                        init_train_on_track(train, (double)train->speed);

                        reply.type = MSG_ACK;
                        printf("Train %d acquired track %d\n", msg.train_id, msg.track_id);
                    } else {
                        // Track is full despite request_track returning true
                        reply.type = MSG_DENY;
                        printf("Train %d denied track %d (track full)\n", msg.train_id, msg.track_id);
                    }
                } else {
                    reply.type = MSG_DENY;
                    printf("Train %d denied track %d\n", msg.train_id, msg.track_id);
                }
                break;
            case MSG_RELEASE_TRACK:
                release_track(msg.train_id, msg.track_id);
                reply.type = MSG_ACK;
                printf("Train %d released track %d\n", msg.train_id, msg.track_id);
                
                // Note: We keep the train data in active_trains in case it requests another track
                // The data will be cleaned up when the system shuts down
                break;
            default:
                printf("Unknown message type from Train %d\n", msg.train_id);
                break;
        }

        // Debug: show current allocation table
        print_resource_status();

        // Release track after processing
        pthread_mutex_unlock(&track_mutex);

        // Send reply back to train
        MsgReply(rcvid, 0, &reply, sizeof(reply));
    }

    // TODO remove this, Cleanup (unreachable in normal execution)
    ChannelDestroy(chid);
    return 0;
}