#include "train_state_machine.h"
#include <stdio.h>
#include "ipc_client.h"

void init_train(train_t *train, int id) {
    train->train_id = id;
    train->current_track = -1;
    train->state = STATE_IDLE;
}

void update_train(train_t *train) {
    switch (train->state) {
        case STATE_IDLE:
            train->state = STATE_REQUESTING;
            break;
        case STATE_REQUESTING:
            // Try to acquire next track
            if (request_track_from_manager(train->train_id, 0)) { // simple demo: always request track 0
                train->current_track = 0;
                train->state = STATE_MOVING;
            }
            break;
        case STATE_MOVING:
            printf("Train %d is moving on track %d\n", train->train_id, train->current_track);
            train->state = STATE_RELEASING;
            break;
        case STATE_RELEASING:
            release_track_to_manager(train->train_id, train->current_track);
            train->current_track = -1;
            train->state = STATE_IDLE;
            break;
    }
}
