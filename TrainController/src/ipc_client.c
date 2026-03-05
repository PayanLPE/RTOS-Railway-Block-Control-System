#include "ipc_client.h"
#include <sys/neutrino.h>
#include <sys/dispatch.h>
#include <stdio.h>
#include "ipc_protocol.h"

#define SERVER_CHID 1

static int coid = -1;

// Ensure we have a connection to the server, if not try to connect
static bool ensure_connection() {
    // If no conenction try for one
    if (coid == -1) {
        coid = ConnectAttach(0, 0, SERVER_CHID, _NTO_SIDE_CHANNEL, 0);
        if (coid == -1) {
            perror("ConnectAttach failed");
            return false;
        }
    }
    return true;
}

// Request a track from the manager, returns true if successful
bool request_track_from_manager(int train_id, int track_id) {
    // Check if we have a connection
    if (!ensure_connection()) return false;

    // Send a request message
    ipc_message_t msg = {MSG_REQUEST_TRACK, train_id, track_id};
    ipc_message_t reply;

    // TODO we should probably check the reply for more info later (priority number, wait time, etc) instead of just ACK/DENY
    if (MsgSend(coid, &msg, sizeof(msg), &reply, sizeof(reply)) == -1) {
        perror("MsgSend failed");
        return false;
    }
    return reply.type == MSG_ACK;
}

// Release a track to the manager, returns true if successful
bool release_track_to_manager(int train_id, int track_id) {
    if (!ensure_connection()) return false;

    // Send a request message
    ipc_message_t msg = {MSG_RELEASE_TRACK, train_id, track_id};
    ipc_message_t reply;

    // TODO we don't actually care about the reply for release, but we should probably check if it was successful
    if (MsgSend(coid, &msg, sizeof(msg), &reply, sizeof(reply)) == -1) {
        perror("MsgSend failed");
        return false;
    }
    return reply.type == MSG_ACK;
}
