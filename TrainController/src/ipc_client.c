#include "ipc_client.h"
#include <sys/neutrino.h>
#include <sys/dispatch.h>
#include <stdio.h>
#include "ipc_protocol.h"

#define SERVER_CHID 1

static int coid = -1;

static bool ensure_connection() {
    if (coid == -1) {
        coid = ConnectAttach(0, 0, SERVER_CHID, _NTO_SIDE_CHANNEL, 0);
        if (coid == -1) {
            perror("ConnectAttach failed");
            return false;
        }
    }
    return true;
}

bool request_track_from_manager(int train_id, int track_id) {
    if (!ensure_connection()) return false;

    ipc_message_t msg = {MSG_REQUEST_TRACK, train_id, track_id};
    ipc_message_t reply;

    if (MsgSend(coid, &msg, sizeof(msg), &reply, sizeof(reply)) == -1) {
        perror("MsgSend failed");
        return false;
    }
    return reply.type == MSG_ACK;
}

bool release_track_to_manager(int train_id, int track_id) {
    if (!ensure_connection()) return false;

    ipc_message_t msg = {MSG_RELEASE_TRACK, train_id, track_id};
    ipc_message_t reply;

    if (MsgSend(coid, &msg, sizeof(msg), &reply, sizeof(reply)) == -1) {
        perror("MsgSend failed");
        return false;
    }
    return reply.type == MSG_ACK;
}
