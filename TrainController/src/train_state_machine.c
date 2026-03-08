#include "train_state_machine.h"
#include <stdio.h>
#include <string.h>
#include "ipc_client.h"
#include "ipc_protocol.h"
#include "system_constants.h"

void init_train(train_data_t *train, int id) {
    if (train == NULL) {
        return;
    }

    memset(train, 0, sizeof(*train));
    train->train_id = id;
    train->track_id = INVALID_TRACK;
}

// Minimal update loop: request a demo track when idle, otherwise release it.
void update_train(train_data_t *train) {
    if (train == NULL) {
        return;
    }

    if (train->track_id == INVALID_TRACK) {
        const int requested_track = 0;
        if (request_track_from_manager(train->train_id, requested_track)) {
            train->track_id = requested_track;
            printf("Train %d acquired track %d\n", train->train_id, train->track_id);
        }
        return;
    }

    printf("Train %d is moving on track %d\n", train->train_id, train->track_id);
    if (release_track_to_manager(train->train_id, train->track_id)) {
        train->track_id = INVALID_TRACK;
    }
}
