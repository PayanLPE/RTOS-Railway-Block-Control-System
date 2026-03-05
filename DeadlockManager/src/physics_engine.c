
#include "physics_engine.h"
#include "ipc_protocol.h"
#include <math.h>
#include <string.h>

// ====================================================================================================
// Physics Engine Implementation
// ====================================================================================================

/**
 * Initialize train position when it enters a track
 * Sets front_position to 0, rear_position to -length, and records entry time
 */
int init_train_on_track(train_data_t *train, double speed) {
    if (train == NULL || speed < 0.0) {
        return -1;
    }

    train->front_position = 0.0;              // Front starts at track beginning
    train->rear_position = -(double)train->length;  // Rear is at negative position
    train->entry_time = time(NULL);           // Current time
    train->current_speed = speed;             // Set the speed
    
    return 0;
}

/**
 * Calculate distance traveled based on speed and elapsed time
 * Distance = speed × time (assumes constant velocity)
 */
double calculate_distance_traveled(double speed, double elapsed_time) {
    if (elapsed_time < 0.0) {
        return 0.0;  // No time has passed yet
    }
    return speed * elapsed_time;
}

/**
 * Update train position based on current time
 * Uses kinematics: position = initial_position + (speed × elapsed_time)
 */
int update_train_position(train_data_t *train, time_t current_time) {
    if (train == NULL) {
        return -1;
    }

    // Calculate elapsed time since train entered track (in seconds)
    double elapsed_time = difftime(current_time, train->entry_time);
    
    if (elapsed_time < 0.0) {
        return -1;  // Invalid time
    }

    // Calculate distance traveled
    double distance_traveled = calculate_distance_traveled(train->current_speed, elapsed_time);
    
    // Update positions (train starts with rear at -length, so front enters at 0)
    train->front_position = distance_traveled;
    train->rear_position = distance_traveled - (double)train->length;
    
    return 0;
}

/**
 * Check if train has completely left the track
 * Train has left when its rear position exceeds track length
 */
int has_train_left_track(train_data_t *train, int track_length) {
    if (train == NULL || track_length <= 0) {
        return 1;
    }
    return (train->rear_position >= (double)track_length) ? 1 : 0;
}

/**
 * Check if two trains on the same track collide
 * Collision occurs if there's overlap in their positions
 */
int check_train_collision(train_data_t *train1, train_data_t *train2) {
    if (train1 == NULL || train2 == NULL) {
        return 0;
    }

    // Check for overlap:
    // Train1 and Train2 collide if:
    // Train1.rear < Train2.front AND Train1.front > Train2.rear
    
    if (train1->rear_position < train2->front_position && 
        train1->front_position > train2->rear_position) {
        return 1;  // Collision detected
    }
    
    return 0;  // No collision
}

/**
 * Calculate distance between two trains on the same track
 * Positive means separated, negative means overlapping (collision)
 */
double get_distance_between_trains(train_data_t *train1, train_data_t *train2) {
    if (train1 == NULL || train2 == NULL) {
        return 0.0;
    }

    // Assuming train1 is ahead, distance is measured from its rear to train2's front
    // This assumes train1.rear_position > train2.front_position
    return train1->rear_position - train2->front_position;
}

/**
 * Calculate time until train completely clears a track section
 * Section is defined by start and end positions
 */
double time_to_clear_section(train_data_t *train, 
                            double section_start, 
                            double section_end) {
    if (train == NULL || train->current_speed <= 0.0) {
        return -1.0;
    }

    // Train rear must clear the section (pass end position)
    // Time = (section_end - rear_position) / speed
    double time_to_clear = (section_end - train->rear_position) / train->current_speed;
    
    // If already cleared or will never reach (negative speed), return -1
    if (time_to_clear < 0.0) {
        return -1.0;
    }
    
    return time_to_clear;
}

/**
 * Calculate track occupancy percentage
 * Occupancy = (total_train_length_on_track / track_length) × 100
 */
double get_track_occupancy(track_data_t *track) {
    if (track == NULL || track->length <= 0) {
        return 0.0;
    }

    double occupied_length = 0.0;

    // Sum up all train lengths that are on the track
    for (int i = 0; i < track->num_trains && i < MAX_TRAINS; i++) {
        train_data_t *train = track->trains[i];
        if (train != NULL) {
            // Calculate the length of train that occupies this track
            // Clamp front to track_length and rear to 0
            double front = train->front_position;
            double rear = train->rear_position;
            
            if (front > 0 && rear < (double)track->length) {
                // Train is partially or fully on the track
                double start = (rear < 0) ? 0 : rear;
                double end = (front > (double)track->length) ? 
                            (double)track->length : front;
                occupied_length += (end - start);
            }
        }
    }

    double occupancy_percent = (occupied_length / (double)track->length) * 100.0;
    
    // Clamp to 0-100%
    if (occupancy_percent > 100.0) {
        occupancy_percent = 100.0;
    }
    if (occupancy_percent < 0.0) {
        occupancy_percent = 0.0;
    }

    return occupancy_percent;
}

/**
 * Calculate when the next train will reach a specific position on track
 * Returns time in seconds until train's front reaches the position
 */
double time_until_train_at_position(track_data_t *track, double position) {
    if (track == NULL || track->num_trains == 0) {
        return -1.0;
    }

    double min_time = -1.0;

    for (int i = 0; i < track->num_trains && i < MAX_TRAINS; i++) {
        train_data_t *train = track->trains[i];
        
        if (train == NULL || train->current_speed <= 0.0) {
            continue;  // Train not available or not moving
        }

        // Time for train front to reach position: time = (position - current_front) / speed
        double time_needed = (position - train->front_position) / train->current_speed;
        
        if (time_needed >= 0.0) {
            // This train will reach the position in the future
            if (min_time < 0.0 || time_needed < min_time) {
                min_time = time_needed;
            }
        }
    }

    return min_time;
}

/**
 * Compute the distance a train has traveled on a track
 * Based on entry time, speed, and length
 */
int compute_distance(track_data_t track_data, train_data_t train_data) {
    // Find the entry time by looking through request records
    time_t entry_time = time(NULL);
    
    for (int i = 0; i < track_data.priority_queue_size; i++) {
        if (track_data.requests[i].train_id == train_data.train_id) {
            entry_time = mktime(&track_data.requests[i].expected_request_time);
            break;
        }
    }

    // Calculate elapsed time
    time_t now = time(NULL);
    double elapsed_time = difftime(now, entry_time);

    // Calculate distance: distance = speed × time
    double distance_traveled = calculate_distance_traveled(
        (double)train_data.speed, 
        elapsed_time
    );

    return (int)distance_traveled;  // Return as integer (in mm)
}

// Updates track data on every tick (train locations)
track_data_t update_track_data(track_data_t track_data) {
    // This function is called every tick to update train positions on the track
    // In a real-time system, this would be triggered by a periodic timer
    
    // Get current time
    time_t current_time = time(NULL);
    
    // For each train on this track, update its position based on:
    // - Entry time into the track
    // - Train speed
    // - Elapsed time since entry
    
    for (int i = 0; i < MAX_TRAINS; i++) {
        if (track_data.trains_id[i] != -1) {
            // TODO: Update train position using physics engine
            // This would call update_train_position() for each train
            // and update any collision detection or boundary checks
        }
    }
    
    return track_data;
}
