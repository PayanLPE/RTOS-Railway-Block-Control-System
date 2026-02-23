#include <stdio.h>
#include <stdlib.h>
#include <sys/neutrino.h>
#include <sys/dispatch.h>
#include "ipc_protocol.h"
#include "resource_manager.h"

#define SERVER_CHID 1

int main() {
    int chid, rcvid;
    ipc_message_t msg;

    init_resource_manager();

    chid = ChannelCreate(0);
    if (chid == -1) {
        perror("ChannelCreate failed");
        return 1;
    }

    printf("DeadlockManager running...\n");

    while (1) {
        rcvid = MsgReceive(chid, &msg, sizeof(msg), NULL);
        if (rcvid == -1) continue;

        ipc_message_t reply;
        reply.type = MSG_DENY;
        reply.train_id = msg.train_id;
        reply.track_id = msg.track_id;

        switch (msg.type) {
            case MSG_REQUEST_TRACK:
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
                break;
        }

        MsgReply(rcvid, 0, &reply, sizeof(reply));
        print_resource_status();
    }

    ChannelDestroy(chid);
    return 0;
}
