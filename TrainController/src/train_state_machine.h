#ifndef TRAIN_STATE_MACHINE_H
#define TRAIN_STATE_MACHINE_H

typedef enum {
    STATE_IDLE,
    STATE_REQUESTING,
    STATE_MOVING,
    STATE_RELEASING
} train_state_t;

typedef struct {
    int train_id;
    int current_track;
    train_state_t state;
} train_t;

void init_train(train_t *train, int id);
void update_train(train_t *train);

#endif // TRAIN_STATE_MACHINE_H
