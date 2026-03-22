#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include "resource_manager.h"
#include "ipc_protocol.h"
#include "system_constants.h"
#include "physics_engine.h"

extern track_data_t track_list[MAX_TRACKS];
extern int track_count;

// Current ownership map: -1 means free, otherwise train_id.
static int track_table[TRACK_COUNT];

static uint64_t get_request_time_ns(void) {
    struct timespec current_time;

    if (clock_gettime(CLOCK_MONOTONIC, &current_time) != 0) {
        return 0;
    }

    return ((uint64_t)current_time.tv_sec * 1000000000ULL) + (uint64_t)current_time.tv_nsec;
}

void init_resource_manager() {
    for (int i = 0; i < TRACK_COUNT; i++) {
        track_table[i] = -1;
    }
}

bool request_track(train_data_t *train, int track_id) {
    int train_id = train->train_id;
    if (track_id < 0 || track_id >= TRACK_COUNT) {
        printf("Train %d: track_id %d out of bounds [0, %d)\n", train_id, track_id, TRACK_COUNT);
        return false;
    }

    if (train->track_id != -1 && train->track_id != track_id) {
        release_track(train_id, train->track_id);
        printf("Train %d released old track %d\n", train_id, train->track_id);
    }

    track_data_t *track = &track_list[track_id];
    uint64_t current_time_ns = get_request_time_ns();

    printf("Train %d requesting track %d: track_id=%d, num_trains=%d\n", 
           train_id, track_id, track->track_id, track->num_trains);

    // Fast path: empty track can be granted immediately.
    if (track->num_trains == 0) {
        track_table[track_id] = train_id;
        printf("Track %d empty, granting to train %d\n", track_id, train_id);
        return true;
    }

    // Gate entry if a train is still near the entrance.
    for (int i = 0; i < track->num_trains; i++) {
        train_data_t *train = track->trains[i];
        if (train != NULL) {
            if (train->front_position < (double)train->length * 2.0) {
                printf("Train %d: denied track %d (entrance blocked by train %d at pos %.0fmm)\n",
                       train_id, track_id, train->train_id, train->front_position);
                return false;
            }
        }
    }

    if (update_trains_on_track(track, current_time_ns) != 0) {
        printf("Train %d: error updating train positions on track %d\n", train_id, track_id);
        return false;  
    }

    // Conservative occupancy cap keeps spacing manageable.
    double occupancy = get_track_occupancy(track);
    printf("Train %d: track %d occupancy %.1f%%\n", train_id, track_id, occupancy);
    if (occupancy > 80.0) {
        printf("Train %d: denied track %d (occupancy too high)\n", train_id, track_id);
        return false;
    }

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

    if (last_train == NULL) {
        track_table[track_id] = train_id;
        return true;
    }

    // Safety buffer (mm) behind the last train before admitting another train.
    const double SAFETY_BUFFER = 1000.0;
    double safe_distance = (double)last_train->length + SAFETY_BUFFER;

    if (min_distance_to_end < safe_distance) {
        printf("Train %d: denied track %d (insufficient space: %.1f < %.1f)\n", 
               train_id, track_id, min_distance_to_end, safe_distance);
        return false;  
    }

    double entry_position = (double)track->length - safe_distance;
    double time_to_safe_entry = time_until_train_at_position(track, entry_position);

    if (time_to_safe_entry < 0.0) {
        track_table[track_id] = train_id;
        return true;
    }

    // Reject if predicted wait is too long for this simple scheduler.
    const double MAX_WAIT_TIME = 300.0;
    if (time_to_safe_entry > MAX_WAIT_TIME) {
        return false;
    }

    track_table[track_id] = train_id;
    printf("Train %d: granted track %d (wait time %.1f seconds)\n", 
           train_id, track_id, time_to_safe_entry);
    return true;
}

void release_track(int train_id, int track_id) {
    if (track_id < 0 || track_id >= TRACK_COUNT) return;
    if (track_table[track_id] == train_id) {
        track_table[track_id] = -1;

        track_data_t *track = &track_list[track_id];
        for (int i = 0; i < track->num_trains; i++) {
            if (track->trains[i] != NULL && track->trains[i]->train_id == train_id) {
                for (int j = i; j < track->num_trains - 1; j++) {
                    track->trains[j] = track->trains[j + 1];
                }
                track->num_trains--;
                break;
            }
        }
    }
}

void print_resource_status() {
    printf("Track status: ");
    for (int i = 0; i < TRACK_COUNT; i++) {
        if (track_table[i] == -1)
            printf("[ ] ");
        else
            printf("[%d] ", track_table[i]);
    }
    printf("\n");

    for (int i = 0; i < track_count; i++) {
        track_data_t *track = &track_list[i];
        if (track->track_id >= 0) {
            double occupancy = get_track_occupancy(track);
            printf("Track %d: %d trains, %.1f%% occupied\n",
                   track->track_id, track->num_trains, occupancy);

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
