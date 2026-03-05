#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include "resource_manager.h"
#include "ipc_protocol.h"
#include "system_constants.h"
#include "physics_engine.h"

// External reference to track list from main.c
extern track_data_t track_list[MAX_TRACKS];
extern int track_count;

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
// Now uses physics engine to determine safe entry times and collision avoidance
// TODO return data containing priority number (num of trains ahead), wait time, etc
// TODO this should accept if the trains are going the same direction and there isnt a train already waiting to go the opposite direction
bool request_track(int train_id, int track_id) {
    if (track_id < 0 || track_id >= TRACK_COUNT) {
        return false;
    }

    track_data_t *track = &track_list[track_id];
    time_t current_time = time(NULL);

    // If track has no trains currently, accept immediately
    if (track->num_trains == 0) {
        track_table[track_id] = train_id;
        return true;
    }

    // Update positions of all trains on the track
    if (update_trains_on_track(track, current_time) != 0) {
        return false;  // Error updating positions
    }

    // Check track occupancy - if over 80%, deny request
    double occupancy = get_track_occupancy(track);
    if (occupancy > 80.0) {
        return false;
    }

    // Calculate safe entry position (leave buffer space from last train)
    // Find the train closest to the end of the track
    double min_distance_to_end = (double)track->length;
    train_data_t *last_train = NULL;

    for (int i = 0; i < track->num_trains; i++) {
        train_data_t *train = track->trains[i];
        if (train != NULL) {
            double distance_to_end = (double)track->length - train->rear_position;
            if (distance_to_end < min_distance_to_end) {
                min_distance_to_end = distance_to_end;
                last_train = train;
            }
        }
    }

    // If no trains found (shouldn't happen), accept
    if (last_train == NULL) {
        track_table[track_id] = train_id;
        return true;
    }

    // Calculate safe following distance (train length + safety buffer)
    const double SAFETY_BUFFER = 1000.0;  // 1 meter safety buffer
    double safe_distance = (double)last_train->length + SAFETY_BUFFER;

    // Check if there's enough space at the end of the track
    if (min_distance_to_end < safe_distance) {
        return false;  // Not enough space
    }

    // Calculate time until the last train clears enough space
    // The new train needs to enter when there's safe_distance available at track end
    double entry_position = (double)track->length - safe_distance;
    double time_to_safe_entry = time_until_train_at_position(track, entry_position);

    // If time_to_safe_entry is -1, no train will reach that position (track is clear)
    if (time_to_safe_entry < 0.0) {
        track_table[track_id] = train_id;
        return true;
    }

    // If the safe entry time is too far in the future (> 5 minutes), deny
    const double MAX_WAIT_TIME = 300.0;  // 5 minutes
    if (time_to_safe_entry > MAX_WAIT_TIME) {
        return false;
    }

    // Accept the request - train can enter safely
    track_table[track_id] = train_id;
    return true;
}

// Release a track held by a train. Only releases if the track is currently held by the requesting train.
void release_track(int train_id, int track_id) {
    if (track_id < 0 || track_id >= TRACK_COUNT) return;
    if (track_table[track_id] == train_id) {
        track_table[track_id] = -1;

        // Remove train from track's train list
        track_data_t *track = &track_list[track_id];
        for (int i = 0; i < track->num_trains; i++) {
            if (track->trains[i] != NULL && track->trains[i]->train_id == train_id) {
                // Shift remaining trains down
                for (int j = i; j < track->num_trains - 1; j++) {
                    track->trains[j] = track->trains[j + 1];
                }
                track->num_trains--;
                break;
            }
        }
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

    // Print detailed physics status for each track
    for (int i = 0; i < track_count; i++) {
        track_data_t *track = &track_list[i];
        if (track->track_id >= 0) {
            double occupancy = get_track_occupancy(track);
            printf("Track %d: %d trains, %.1f%% occupied\n",
                   track->track_id, track->num_trains, occupancy);

            // Print train positions
            for (int j = 0; j < track->num_trains; j++) {
                train_data_t *train = track->trains[j];
                if (train != NULL) {
                    printf("  Train %d: front=%.0fmm, rear=%.0fmm, speed=%.0fmm/s\n",
                           train->train_id, train->front_position,
                           train->rear_position, train->current_speed);
                }
            }
        }
    }
    printf("\n");
}
