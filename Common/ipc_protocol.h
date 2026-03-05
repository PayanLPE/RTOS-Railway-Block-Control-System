#ifndef IPC_PROTOCOL_H
#define IPC_PROTOCOL_H

#include <stdint.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

// TODO do we need this? should later replace with congestion percentage computations
#define MAX_TRACKS 10
#define MAX_TRAINS 5
#define MAX_TRACK_ENDPOINTS 4 // I.e. a fork on both sides of a single track >-----<

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

// Track data
typedef struct {
    int track_id;
    int trains_id[MAX_TRAINS]; // List of trains on the tracks in order
    int length;
    int direction; // Direction of all trains on the track
    int endpoints[MAX_TRACK_ENDPOINTS]; // List of track IDs that are connected to this track (for routing purposes)
    // TODO make a priority queue of request_record_t below
} track_data_t;

// Request data for tracks
typedef struct {
    int train_id;
    struct tm expected_request_time; // when the train needs the track
    struct tm expected_release_time; // When other trains can advance to the track
} request_record_t;

// Train data
typedef struct {
    int train_id;
    int track_id; // Current track the train is on,. -1 if not on any track
    int destination; // Destination track the train wants to go to (TODO needs routing algorithm)
    int speed; // Speed of the train (for future use, not currently implemented)
    int length; // Length of the train (for future use, not currently implemented)
    // Don't need distance since we can compute it based on entry time and speed
    // Assumes constant speed, which is not realistic but simplifies the problem for now
} train_data_t;

#endif // IPC_PROTOCOL_H
