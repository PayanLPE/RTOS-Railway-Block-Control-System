#ifndef IPC_PROTOCOL_H
#define IPC_PROTOCOL_H

#include <stdint.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TRACKS 10
#define MAX_TRAINS 5
#define MAX_TRACK_ENDPOINTS 4 
#define MAX_TRACK_CHANGES 50 
#define MAX_TRACK_REQUESTS 20   

// Shared IPC message types between TrainController and DeadlockManager.
typedef enum {
    MSG_REQUEST_TRACK,
    MSG_RELEASE_TRACK,
    MSG_ACK,
    MSG_DENY,
    MSG_GET_TRAIN_DATA,    
    MSG_TRAIN_DATA_REPLY,  
    MSG_POSITION_UPDATE     
} message_type_t;

typedef struct {
    message_type_t type;
    int train_id;
    int track_id;
} ipc_message_t;

typedef struct track_data_s track_data_t;
typedef struct train_data_s train_data_t;

typedef struct request_record_s {
    int train_id;
    track_data_t *current_track; 
    struct tm expected_request_time; 
    struct tm expected_release_time; 
} request_record_t;

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
    request_record_t requests[MAX_TRACK_REQUESTS];
    int priority_queue_size; 
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

static time_t get_track_next_available_time(track_data_t *track);

// Compute scheduling priority from requested arrival and current queue state.
static int compute_request_priority(request_record_t *req) {
    if (req == NULL || req->current_track == NULL) {
        return -1; 
    }

    time_t now = time(NULL);
    struct tm *current_time = localtime(&now);
    
    time_t request_time = mktime(&req->expected_request_time);
    time_t current_actual = mktime(current_time);
    
    double time_until_needed = difftime(request_time, current_actual);
    
    if (time_until_needed < 0) {
        time_until_needed = 0;
    }
    
    time_t track_next_available = get_track_next_available_time(req->current_track);
    
    const int BUFFER_TIME = 30; 
    
    time_t earliest_schedule_time;
    if (track_next_available > current_actual) {
        earliest_schedule_time = track_next_available + BUFFER_TIME;
    } else {
        earliest_schedule_time = current_actual + BUFFER_TIME;
    }
    
    time_t earliest = mktime(localtime(&earliest_schedule_time));
    time_t requested = mktime(&req->expected_request_time);
    
    double schedule_gap = difftime(requested, earliest);
    
    if (schedule_gap < 0) {
        double max_speedup_factor = 0.2; 
        
        double time_needed = -schedule_gap; 
        double required_speedup = time_needed / time_until_needed;
        
        if (required_speedup <= max_speedup_factor) {
            time_t adjusted_time = requested;
            return (int)difftime(adjusted_time, current_actual);
        } else {
            return -1;
        }
    }
    else {
        double max_slowdown_factor = 0.3; 
        
        if (schedule_gap > 0) {
            double required_slowdown = schedule_gap / time_until_needed;
            
            if (required_slowdown <= max_slowdown_factor) {
                return (int)difftime(requested, current_actual);
            } else {
                return (int)(difftime(earliest, current_actual) + schedule_gap);
            }
        }
    }
    
    return (int)time_until_needed;
}


// Estimate when a track becomes free based on the latest queued request.
static time_t get_track_next_available_time(track_data_t *track) {
    if (track == NULL || track->priority_queue_size == 0) {
        return time(NULL); 
    }
    
    request_record_t *last_req = &track->requests[track->priority_queue_size - 1];
    
    time_t request_time = mktime(&last_req->expected_request_time);
    time_t release_time = mktime(&last_req->expected_release_time);
    time_t duration = difftime(release_time, request_time);
    
    return request_time + duration;
}


// Initialize an empty per-track request queue.
static inline void init_track_queue(track_data_t *track) {
    track->priority_queue_size = 0;
}


// Insert request ordered by expected arrival time (earlier first).
static inline int enqueue_track_request(track_data_t *track, request_record_t req) {
    if (track->priority_queue_size >= MAX_TRACK_REQUESTS) return 1;

    int priority = compute_request_priority(&req);
    
    if (priority == -1) return 1;

    int insert_pos = 0;
    while (insert_pos < track->priority_queue_size) {
        time_t current_req_time = mktime(&track->requests[insert_pos].expected_request_time);
        time_t new_req_time = mktime(&req.expected_request_time);
        
        if (difftime(current_req_time, new_req_time) > 0) {
            break;
        }
        insert_pos++;
    }
    
    for (int i = track->priority_queue_size; i > insert_pos; i--) {
        track->requests[i] = track->requests[i - 1];
    }
    
    track->requests[insert_pos] = req;
    track->priority_queue_size++;
    return 0;
}


// Remove the highest-priority request from the queue front.
static inline int dequeue_track_request(track_data_t *track) {
    if (track->priority_queue_size == 0) return 1;

    for (int i = 1; i < track->priority_queue_size; i++) {
        track->requests[i - 1] = track->requests[i];
    }

    track->priority_queue_size--;

    return 0; 
}


// Debug helper to inspect queue ordering.
static inline void print_track_queue(track_data_t *track) {
    printf("Track %d Queue (size=%d):\n", track->track_id, track->priority_queue_size);
    for (int i = 0; i < track->priority_queue_size; i++) {
        time_t req_time = mktime(&track->requests[i].expected_request_time);
        printf("  [%d] Train %d at %s", i, track->requests[i].train_id, ctime(&req_time));
    }
}

#endif
