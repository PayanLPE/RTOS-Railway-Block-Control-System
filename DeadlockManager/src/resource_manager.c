#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "resource_manager.h"
#include "ipc_protocol.h"
#include "system_constants.h"

// Resource table: -1 = free, else train_id
static int track_table[TRACK_COUNT];

// Initialize the resource manager by marking all tracks as free
// TODO read a file/database to initialize a bunch of tracks, also create an endpoint so we can add/remove tracks maybe?
// TODO trains will request upcoming tracks in advance, holds a priority list per track for trains
void init_resource_manager() {
    for (int i = 0; i < TRACK_COUNT; i++) {
        track_table[i] = -1;
    }
}

// Request a track for a train. Returns true if successful, false if the track is occupied or invalid.
// TODO return data containing priority number (num of trains ahead), wait time, etc
// TODO this should accept if the trains are going the same direction and there isnt a train already waiting to go the opposite direction
bool request_track(int train_id, int track_id) {
    if (track_id < 0 || track_id >= TRACK_COUNT) return false;
    if (track_table[track_id] == -1) {
        track_table[track_id] = train_id;
        return true;
    }
    return false;
}

// Release a track held by a train. Only releases if the track is currently held by the requesting train.
void release_track(int train_id, int track_id) {
    if (track_id < 0 || track_id >= TRACK_COUNT) return;
    if (track_table[track_id] == train_id) {
        track_table[track_id] = -1;
    }
}

// Prints the whole resource table for debugging purposes
void print_resource_status() {
    printf("Track status: ");
    for (int i = 0; i < TRACK_COUNT; i++) {
        if (track_table[i] == -1)
            printf("[ ] ");
        else
            printf("[%d] ", track_table[i]);
    }
    printf("\n");
}
