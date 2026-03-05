#include <stdio.h>
#include <stdlib.h>
#include <sys/neutrino.h>
#include <sys/dispatch.h>
#include "ipc_protocol.h"
#include "resource_manager.h"

#define SERVER_CHID 1 // TODO Do we even need this?

int main() {
    int chid; // Channel ID for receiving messages from trains
    int rcvid; // Receiver ID for replying to messages
    ipc_message_t msg; // Buffer for incoming messages from trains

    // Initialize the resource manager (track table)
    init_resource_manager();

    // Create a channel for receiving messages from trains
    // Clients (trains) will connect to this channel to request/release tracks
    chid = ChannelCreate(0);
    if (chid == -1) {
        perror("ChannelCreate failed");
        return 1;
    }

    printf("DeadlockManager running...\n");

    // Wait for messages from trains and process them in an infinite loop
    // TODO change to tick formation
    while (1) {
        // Get message if there is one
        rcvid = MsgReceive(chid, &msg, sizeof(msg), NULL);
        if (rcvid == -1) continue;

        ipc_message_t reply; // Reply message
        reply.type = MSG_DENY; // Default to deny
        reply.train_id = msg.train_id;
        reply.track_id = msg.track_id;

        // Process the message request based on the type (request or release)
        switch (msg.type) {
            case MSG_REQUEST_TRACK:
                // TODO later change this to return more info (priority number, wait time, etc) instead of just ACK/DENY
                if (request_track(msg.train_id, msg.track_id)) {
                    reply.type = MSG_ACK;
                    printf("Train %d acquired track %d\n", msg.train_id, msg.track_id);
                } else {
                    printf("Train %d denied track %d\n", msg.train_id, msg.track_id);
                }
                break;
            case MSG_RELEASE_TRACK:
                release_track(msg.train_id, msg.track_id);
                reply.type = MSG_ACK;
                printf("Train %d released track %d\n", msg.train_id, msg.track_id);
                break;
            default:
                // Invalid type
                // No changes to the reply, defaults to MSG_DENY
                break;
        }

        // Return the reply to the sender (train)
        MsgReply(rcvid, 0, &reply, sizeof(reply));
        print_resource_status(); // For debugging
    }

    // Kill the channel
    ChannelDestroy(chid);
    return 0;
}
