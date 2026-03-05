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

// Mutex protecting the resource manager
pthread_mutex_t track_mutex = PTHREAD_MUTEX_INITIALIZER;

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

        // Initialize trains on track to -1 (empty)
        for (int i = 0; i < MAX_TRAINS; i++) {
            track.trains_id[i] = -1;
        }

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
    while (1) {
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
                if (request_track(msg.train_id, msg.track_id)) {
                    reply.type = MSG_ACK;
                    printf("Train %d acquired track %d\n", msg.train_id, msg.track_id);
                } else {
                    printf("Train %d denied track %d\n", msg.train_id, msg.track_id);
                }
                break;
            case MSG_RELEASE_TRACK:
                release_track(msg.train_id, msg.track_id);
                reply.type = MSG_ACK;
                printf("Train %d released track %d\n", .train_id, msg.track_id);
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