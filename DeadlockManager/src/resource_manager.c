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
bool request_track(train_data_t *train, int track_id) {
    int train_id = train->train_id;
    if (track_id < 0 || track_id >= TRACK_COUNT) {
        printf("Train %d: track_id %d out of bounds [0, %d)\n", train_id, track_id, TRACK_COUNT);
        return false;
    }

    // If train is already on a track, release it first
    if (train->track_id != -1 && train->track_id != track_id) {
        release_track(train_id, train->track_id);
        printf("Train %d released old track %d\n", train_id, train->track_id);
    }

    track_data_t *track = &track_list[track_id];
    time_t current_time = time(NULL);

    printf("Train %d requesting track %d: track_id=%d, num_trains=%d\n", 
           train_id, track_id, track->track_id, track->num_trains);

    // If track has no trains currently, accept immediately
    if (track->num_trains == 0) {
        track_table[track_id] = train_id;
        printf("Track %d empty, granting to train %d\n", track_id, train_id);
        return true;
    }

    // Check if any train is currently entering the track (blocking entrance)
    for (int i = 0; i < track->num_trains; i++) {
        train_data_t *train = track->trains[i];
        if (train != NULL) {
            // If a train's front hasn't cleared its own length, entrance is blocked
            if (train->front_position < (double)train->length * 2.0) {
                printf("Train %d: denied track %d (entrance blocked by train %d at pos %.0fmm)\n",
                       train_id, track_id, train->train_id, train->front_position);
                return false;
            }
        }
    }

    // Update positions of all trains on the track
    if (update_trains_on_track(track, current_time) != 0) {
        printf("Train %d: error updating train positions on track %d\n", train_id, track_id);
        return false;  // Error updating positions
    }

    // Check track occupancy - if over 80%, deny request
    double occupancy = get_track_occupancy(track);
    printf("Train %d: track %d occupancy %.1f%%\n", train_id, track_id, occupancy);
    if (occupancy > 80.0) {
        printf("Train %d: denied track %d (occupancy too high)\n", train_id, track_id);
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
        printf("Train %d: denied track %d (insufficient space: %.1f < %.1f)\n", 
               train_id, track_id, min_distance_to_end, safe_distance);
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
    printf("Train %d: granted track %d (wait time %.1f seconds)\n", 
           train_id, track_id, time_to_safe_entry);
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
