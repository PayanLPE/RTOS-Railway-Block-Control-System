#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/neutrino.h>
#include <sys/wait.h>
#include "ipc_protocol.h"

#define CONFIG_LINE_MAX 256

// TODO change this
#define DEADLOCK_MANAGER_PID 1234   // Replace with actual PID
#define DEADLOCK_MANAGER_CHID 1     // Replace with actual CHID

// Function to run the train process
void run_train_process(train_data_t train) {
    printf("Train %d starting at track %d heading to %d\n", train.train_id, train.track_id, train.destination);

    // Connect to DeadlockManager
    int coid = ConnectAttach(0, DEADLOCK_MANAGER_PID, DEADLOCK_MANAGER_CHID, 0, 0);
    if (coid == -1) {
        perror("ConnectAttach failed");
        exit(1);
    }

    // TODO make this a loop based on tick formulation until the train reaches its destination in which we can kill the process for the train

    // TODO this would be based on destination and starting track
    // Example: Request starting track
    ipc_message_t msg;
    msg.type = MSG_REQUEST_TRACK;
    msg.train_id = train.train_id;
    msg.track_id = train.track_id;

    ipc_message_t reply;

    // Check reply
    if (MsgSend(coid, &msg, sizeof(msg), &reply, sizeof(reply)) == -1) {
        perror("MsgSend failed");
        exit(1);
    }

    // Print result of track request
    if (reply.type == MSG_ACK) {
        printf("Train %d acquired track %d\n", train.train_id, train.track_id);
    } else {
        printf("Train %d denied track %d\n", train.train_id, train.track_id);
    }

    // Infinite movement loop would go here
    // TODO Replace with tick formulation
    while (1) {
        sleep(1);
    }
}

// Main function to read config and spawn train processes
// Usage: ./train_controller <train_config_file>
int main(int argc, char *argv[]) {
    // Ensure valid input
    if (argc != 2) {
        printf("Usage: %s <train_config_file>\n", argv[0]);
        return 1;
    }

    // Open config file
    FILE *file = fopen(argv[1], "r");
    if (!file) {
        perror("Failed to open config file");
        return 1;
    }

    // Loop through config lines
    char line[CONFIG_LINE_MAX];
    while (fgets(line, sizeof(line), file)) {
        // Skip comments and empty lines
        if (line[0] == '#' || strlen(line) < 3)
            continue;

        // Parse train data
        train_data_t train;
        if (sscanf(line, "%d %d %d %d %d", &train.train_id, &train.track_id, &train.destination, &train.speed, &train.length) != 5) {
            continue;
        }

        // Spawn a process for each train
        pid_t pid = fork();
        if (pid == 0) {
            // CHILD
            run_train_process(train);
            exit(0);
        } else if (pid < 0) {
            perror("fork failed");
        } else {
            // PARENT
            printf("Spawned Train %d (PID %d)\n", train.train_id, pid);
        }
    }
    fclose(file);

    // TODO Parent waits for children (optional)
    while (wait(NULL) > 0);
    return 0;
}