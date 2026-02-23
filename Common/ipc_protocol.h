#ifndef IPC_PROTOCOL_H
#define IPC_PROTOCOL_H

#include <stdint.h>

// TODO do we need this? should later replace with congestion percentage computations
#define MAX_TRACKS 10
#define MAX_TRAINS 5

// Message types
typedef enum {
    MSG_REQUEST_TRACK,
    MSG_RELEASE_TRACK,
    MSG_ACK,
    MSG_DENY
} message_type_t;

// IPC message structure
typedef struct {
    message_type_t type;
    int train_id;
    int track_id;
} ipc_message_t;

#endif // IPC_PROTOCOL_H
