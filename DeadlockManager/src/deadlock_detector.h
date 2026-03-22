#ifndef DEADLOCK_DETECTOR_H
#define DEADLOCK_DETECTOR_H

#include "ipc_protocol.h"
#include <time.h>
#include <stdbool.h>

// Deadlock detection and recovery types
typedef enum {
    TRAIN_WAITING,
    TRAIN_ACTIVE,
    TRAIN_STUCK
} train_state_t;

typedef struct {
    int train_id;
    train_state_t state;
    int currently_holds_track;      // Track currently owned (or -1)
    int currently_wants_track;      // Track being requested (or -1)
    uint64_t request_time_ns;       // When the current request was made
    int retry_count;
} train_request_state_t;

typedef struct {
    int train_id;
    int track_id;
    uint64_t time_held_ns;          // How long train has held this track
} track_occupancy_record_t;

// Deadlock detection interface
int detect_deadlock(train_request_state_t *trains, int train_count,
                   track_data_t *tracks, int track_count);

int detect_stuck_train(train_request_state_t *train_state, uint64_t current_time_ns);

int suggest_reroute(int blocked_train_id, train_data_t *trains, int train_count,
                   track_data_t *tracks, int track_count,
                   int current_destination, int *suggested_track);

void initialize_train_request_state(train_request_state_t *state, int train_id);

void update_train_request_state(train_request_state_t *state,
                               int holds_track, int wants_track,
                               uint64_t current_time_ns);

void print_deadlock_graph(train_request_state_t *trains, int train_count);

#endif
