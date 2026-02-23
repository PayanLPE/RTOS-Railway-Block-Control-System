#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "resource_manager.h"
#include "ipc_protocol.h"
#include "system_constants.h"

// Resource table: -1 = free, else train_id
static int track_table[TRACK_COUNT];

void init_resource_manager() {
    for (int i = 0; i < TRACK_COUNT; i++) {
        track_table[i] = -1;
    }
}

bool request_track(int train_id, int track_id) {
    if (track_id < 0 || track_id >= TRACK_COUNT) return false;
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
}
