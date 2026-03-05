#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "train_state_machine.h"

// Manually create the trains 1 by 1 by calling <./TrainController 1 &>, <./TrainController 2 &>, etc.

int main(int argc, char *argv[]) {
    // Grab train ID
    int train_id = (argc > 1) ? atoi(argv[1]) : 1;
    train_t train;

    // Initialize the train data
    init_train(&train, train_id);

    // Infinitly update the train state
    // TODO change to tick formation
    while (1) {
        update_train(&train);
        sleep(1); // TODO change to tick formation, simulate time passing
    }

    return 0;
}
