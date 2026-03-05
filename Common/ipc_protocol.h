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
#define MAX_TRACK_CHANGES 50 // The number of track changes
#define MAX_TRACK_REQUESTS 20   // TODO max queued requests per track

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

// Forward declaration for track_data_t, prevents cirular dependency issues with request_record_t
typedef struct track_data_s track_data_t;

// Track data
// TODO make this into a linked list structure to point to neighboring tracks
typedef struct track_data_s {
    int track_id;
    train_data_t* trains[MAX_TRAINS]; // List of pointers to actual train data on this track
    int num_trains; // Number of trains currently on this track
    int length;
    int direction; // Direction of all trains on the track
    int endpoints[MAX_TRACK_ENDPOINTS]; // List of track IDs that are connected to this track (for routing purposes)
    // Priority queue of track requests, sorted by expected request time (soonest first)
    request_record_t requests[MAX_TRACK_REQUESTS];
    int priority_queue_size; // Number of requests in the priority queue
} track_data_t;

// Request data for tracks
typedef struct {
    int train_id;
    track_data_t *current_track; // Current track the train is on
    struct tm expected_request_time; // when the train needs the track
    struct tm expected_release_time; // When other trains can advance to the track
} request_record_t;

// Train data
typedef struct {
    int train_id;
    int track_id; // Current track the train is on, -1 if not on any track
    int destination; // Destination track the train wants to go to (TODO needs routing algorithm)
    
    // TODO might change when we make track data a linked list
    // TODO needs routing algorithm which depends on linked list struct
    int route[MAX_TRACK_CHANGES];
    int speed; // Speed of the train in mm/s
    int length; // Length of the train in mm
    
    // ====== Physics Engine Data ======
    // Train position on track
    double front_position;      // Position of train front from track start (mm)
    double rear_position;       // Position of train rear from track start (mm)
    time_t entry_time;          // When train entered current track (seconds)
    double current_speed;       // Current speed of train (mm/s) - may differ from nominal speed
} train_data_t;



// ====================================================================================================
// Priority Queue
// ====================================================================================================

// Computes the request priority based on arrival of trains
// Returns -1 if request cannot be scheduled, otherwise returns priority value
// Lower return value = higher priority (sooner arrival)
static int compute_request_priority(request_record_t *req) {
    if (req == NULL || req->current_track == NULL) {
        return -1; // Invalid request
    }

    time_t now = time(NULL);
    struct tm *current_time = localtime(&now);
    
    // Convert tm structs to time_t for easier comparison
    time_t request_time = mktime(&req->expected_request_time);
    time_t current_actual = mktime(current_time);
    
    // Calculate time until request needed (in seconds)
    double time_until_needed = difftime(request_time, current_actual);
    
    // If request time is in the past, it's already late
    if (time_until_needed < 0) {
        // Give it highest priority but still check if it can be scheduled
        time_until_needed = 0;
    }
    
    // Get the track's next available time based on current queue
    time_t track_next_available = get_track_next_available_time(req->current_track);
    double time_until_track_available = difftime(track_next_available, current_actual);
    
    // Buffer time between trains (in seconds) - configurable
    const int BUFFER_TIME = 30; // 30 seconds buffer between trains
    
    // Calculate the earliest this train can be scheduled
    time_t earliest_schedule_time;
    if (track_next_available > current_actual) {
        earliest_schedule_time = track_next_available + BUFFER_TIME;
    } else {
        earliest_schedule_time = current_actual + BUFFER_TIME;
    }
    
    // Convert to time_t for comparison
    time_t earliest = mktime(localtime(&earliest_schedule_time));
    time_t requested = mktime(&req->expected_request_time);
    
    // Calculate the difference between requested and earliest possible
    double schedule_gap = difftime(requested, earliest);
    
    // If the train wants to arrive before the track is available (+buffer)
    if (schedule_gap < 0) {
        // Check if we can adjust speed to make it work
        // Allow up to 20% speedup to fit into schedule
        double max_speedup_factor = 0.2; // 20% faster
        
        // Calculate how much speedup would be needed
        double time_needed = -schedule_gap; // How much earlier we need it
        double required_speedup = time_needed / time_until_needed;
        
        if (required_speedup <= max_speedup_factor) {
            // Can speed up to fit - still schedule it
            // Priority based on adjusted time
            time_t adjusted_time = requested;
            return (int)difftime(adjusted_time, current_actual);
        } else {
            // Cannot speed up enough - deny request
            return -1;
        }
    }
    // If the train wants to arrive after track is available
    else {
        // Check if we can slow down to match schedule (up to 30% slower)
        double max_slowdown_factor = 0.3; // 30% slower
        
        if (schedule_gap > 0) {
            double required_slowdown = schedule_gap / time_until_needed;
            
            if (required_slowdown <= max_slowdown_factor) {
                // Can slow down to fit - schedule it
                // Priority based on requested time
                return (int)difftime(requested, current_actual);
            } else {
                // Too much slowdown needed - still schedule but with lower priority
                // Or could return -1 if you want to deny extreme slowdowns
                return (int)(difftime(earliest, current_actual) + schedule_gap);
            }
        }
    }
    
    // Default: return time until needed as priority (sooner = higher priority)
    return (int)time_until_needed;
}



// Helper function to calculate track's next available time
static time_t get_track_next_available_time(track_data_t *track) {
    if (track == NULL || track->priority_queue_size == 0) {
        return time(NULL); // Track is available now
    }
    
    // Get the last request in the queue (latest arrival)
    request_record_t *last_req = &track->requests[track->priority_queue_size - 1];
    
    // Return its request time as the track's next busy time
    // Add expected duration of track usage
    time_t request_time = mktime(&last_req->expected_request_time);
    time_t release_time = mktime(&last_req->expected_release_time);
    time_t duration = difftime(release_time, request_time);
    
    return request_time + duration;
}



// Initializes the track's request queue
void init_track_queue(track_data_t *track) {
    track->priority_queue_size = 0;
}



// Enqueues a track request into the track's priority queue
// Returns 0 on success, 1 on failure
int enqueue_track_request(track_data_t *track, request_record_t req) {
    // Deny if no space in the queue
    if (track->priority_queue_size >= MAX_TRACK_REQUESTS) return 1;

    // Compute priority of the new request
    int priority = compute_request_priority(&req);
    
    // Request declined (unable to schedule with reasonable speed adjustments)
    if (priority == -1) return 1;

    // Find insertion point based on arrival time (sooner = higher priority)
    int insert_pos = 0;
    while (insert_pos < track->priority_queue_size) {
        time_t current_req_time = mktime(&track->requests[insert_pos].expected_request_time);
        time_t new_req_time = mktime(&req.expected_request_time);
        
        // If current request has later arrival time, insert before it
        if (difftime(current_req_time, new_req_time) > 0) {
            break;
        }
        insert_pos++;
    }
    
    // Shift lower priority (later arrival) requests down
    for (int i = track->priority_queue_size; i > insert_pos; i--) {
        track->requests[i] = track->requests[i - 1];
    }
    
    // Insert new request
    track->requests[insert_pos] = req;
    track->priority_queue_size++;
    return 0;
}



// Dequeues the highest priority request from the track's priority queue
// I.E. Train is now on the track and no longer is a request or got rerouted
// Returns 0 on success, 1 on failure
int dequeue_track_request(track_data_t *track) {
    // Deny if no requests in the queue
    if (track->priority_queue_size == 0) return 1;

    // Shift all requests up the queue (remove first element)
    for (int i = 1; i < track->priority_queue_size; i++) {
        track->requests[i - 1] = track->requests[i];
    }

    track->priority_queue_size--;

    return 0; // Return 0 for success
}



// Optional: Helper function to print the queue order
void print_track_queue(track_data_t *track) {
    printf("Track %d Queue (size=%d):\n", track->track_id, track->priority_queue_size);
    for (int i = 0; i < track->priority_queue_size; i++) {
        time_t req_time = mktime(&track->requests[i].expected_request_time);
        printf("  [%d] Train %d at %s", i, track->requests[i].train_id, ctime(&req_time));
    }
}

#endif // IPC_PROTOCOL_H
