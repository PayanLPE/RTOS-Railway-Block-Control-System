#ifndef TRAIN_STATE_MACHINE_H
#define TRAIN_STATE_MACHINE_H

#include "ipc_protocol.h"
#include "system_constants.h"

typedef enum {
    STATE_IDLE,
    STATE_REQUESTING,
    STATE_MOVING,
    STATE_RELEASING
} train_state_t;

void init_train(track_data_t *train, int id);
void update_train(track_data_t *train);

#endif // TRAIN_STATE_MACHINE_H
