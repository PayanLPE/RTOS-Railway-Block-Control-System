#ifndef IPC_PROTOCOL_H
#define IPC_PROTOCOL_H

#include <stdint.h>

#define MAX_TRACKS 10
#define MAX_TRAINS 5
#define MAX_TRACK_ENDPOINTS 4 
#define MAX_TRACK_CHANGES 50 

// Shared IPC message types between TrainController and DeadlockManager.
typedef enum {
    MSG_REQUEST_TRACK,
    MSG_RELEASE_TRACK,
    MSG_ACK,
    MSG_DENY,
    MSG_GET_TRAIN_DATA,    
    MSG_TRAIN_DATA_REPLY,  
    MSG_POSITION_UPDATE,
    MSG_REROUTE_SUGGESTION  // Deadlock recovery: suggestion to take alternate route
} message_type_t;

typedef struct {
    message_type_t type;
    int train_id;
    int track_id;
} ipc_message_t;

typedef struct track_data_s track_data_t;
typedef struct train_data_s train_data_t;

// Runtime train state that can be exchanged across processes.
typedef struct train_data_s {
    int train_id;
    int track_id; 
    int destination; 
    
    int route[MAX_TRACK_CHANGES];
    int speed; 
    int length; 
    
    double front_position;      
    double rear_position;       
    uint64_t entry_time_ns;     
    double current_speed;       
} train_data_t;

// Track model includes active trains plus pending requests.
typedef struct track_data_s {
    int track_id;
    train_data_t* trains[MAX_TRAINS]; 
    int num_trains; 
    int length;
    int direction; 
    int endpoints[MAX_TRACK_ENDPOINTS]; 
} track_data_t;

typedef struct {
    message_type_t type;
    int train_id;
    train_data_t train_data;  
} train_query_message_t;

typedef struct {
    message_type_t type;
    int train_id;
    int track_id;
    int track_length;      
    double front_position;      
    double rear_position;       
    double current_speed;       
    uint64_t tick_time_ns;      
    unsigned long long tick_count;
} position_update_message_t;

#endif
