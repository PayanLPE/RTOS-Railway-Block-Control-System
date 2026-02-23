#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "train_state_machine.h"

int main(int argc, char *argv[]) {
    int train_id = (argc > 1) ? atoi(argv[1]) : 1;
    train_t train;

    init_train(&train, train_id);

    while (1) {
        update_train(&train);
        sleep(1); // simulate time passing
    }

    return 0;
}
