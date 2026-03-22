#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "resource_manager.h"
#include "ipc_protocol.h"
#include "system_constants.h"

extern track_data_t track_list[MAX_TRACKS];
extern int track_count;

// Current ownership map: -1 means free, otherwise train_id.
static int track_table[TRACK_COUNT];

void init_resource_manager() {
    for (int i = 0; i < TRACK_COUNT; i++) {
        track_table[i] = -1;
    }
}

bool request_track(train_data_t *train, int track_id) {
    if (train == NULL) {
        return false;
    }

    int train_id = train->train_id;
    if (track_id < 0 || track_id >= TRACK_COUNT) {
        printf("[DENY] train=%d track=%d reason=invalid-track\n", train_id, track_id);
        return false;
    }

    if (train->track_id == track_id) {
        track_table[track_id] = train_id;
        return true;
    }

    // Block model: exactly one train can occupy a track at a time.
    if (track_table[track_id] == -1) {
        track_table[track_id] = train_id;
        return true;
    }

    return false;
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
    printf("[STATE] ");
    for (int i = 0; i < TRACK_COUNT; i++) {
        int owner = track_table[i];

        if (owner == -1) {
            printf("T%d[free] ", i);
        } else {
            printf("T%d[owner=%d] ", i, owner);
        }
    }
    printf("\n");
}
