
#include "physics_engine.h"
#include "ipc_protocol.h"
#include <math.h>
#include <string.h>

#define NSEC_PER_SEC 1000000000ULL


// Monotonic timestamp helper used by all motion calculations.


uint64_t get_current_time_ns(void) {
    struct timespec current_time;

    if (clock_gettime(CLOCK_MONOTONIC, &current_time) != 0) {
        return 0;
    }

    return ((uint64_t)current_time.tv_sec * NSEC_PER_SEC) + (uint64_t)current_time.tv_nsec;
}

int init_train_on_track(train_data_t *train, double speed) {
    if (train == NULL || speed < 0.0) {
        return -1;
    }

    // Place front at entry and rear behind start by train length.
    train->front_position = 0.0;
    train->rear_position = -(double)train->length;
    train->entry_time_ns = get_current_time_ns();
    train->current_speed = speed;             
    
    return 0;
}


double calculate_distance_traveled(double speed, double elapsed_time) {
    if (elapsed_time < 0.0) {
        return 0.0;  
    }
    return speed * elapsed_time;
}


int update_train_position(train_data_t *train, uint64_t current_time_ns) {
    if (train == NULL) {
        return -1;
    }

    if (current_time_ns < train->entry_time_ns) {
        return -1;
    }

    // Constant-velocity model: position = speed * elapsed_time.
    double elapsed_time = (double)(current_time_ns - train->entry_time_ns) / (double)NSEC_PER_SEC;
    
    if (elapsed_time < 0.0) {
        return -1;  
    }

    double distance_traveled = calculate_distance_traveled(train->current_speed, elapsed_time);
    
    train->front_position = distance_traveled;
    train->rear_position = distance_traveled - (double)train->length;
    
    return 0;
}


int has_train_left_track(train_data_t *train, int track_length) {
    if (train == NULL || track_length <= 0) {
        return 1;
    }
    return (train->rear_position >= (double)track_length) ? 1 : 0;
}


int check_train_collision(train_data_t *train1, train_data_t *train2) {
    if (train1 == NULL || train2 == NULL) {
        return 0;
    }

        // Overlap test on 1D intervals [rear, front].
    if (train1->rear_position < train2->front_position && 
        train1->front_position > train2->rear_position) {
        return 1;  
    }
    
    return 0;  
}


double get_distance_between_trains(train_data_t *train1, train_data_t *train2) {
    if (train1 == NULL || train2 == NULL) {
        return 0.0;
    }

    return train1->rear_position - train2->front_position;
}


double time_to_clear_section(train_data_t *train, 
                            double section_start, 
                            double section_end) {
    if (train == NULL || train->current_speed <= 0.0) {
        return -1.0;
    }

    double time_to_clear = (section_end - train->rear_position) / train->current_speed;
    
    if (time_to_clear < 0.0) {
        return -1.0;
    }
    
    return time_to_clear;
}


int update_trains_on_track(track_data_t *track, uint64_t current_time_ns) {
    if (track == NULL) {
        return -1;
    }

    // Compact train array while removing entries that fully left the track.
    int write_idx = 0;
    for (int i = 0; i < track->num_trains && i < MAX_TRAINS; i++) {
        train_data_t *train = track->trains[i];
        if (train == NULL) {
            continue;
        }

        if (update_train_position(train, current_time_ns) != 0) {
            return -1;
        }

        if (!has_train_left_track(train, track->length)) {
            track->trains[write_idx++] = train;
        } else {
            train->track_id = -1;
        }
    }

    for (int i = write_idx; i < MAX_TRAINS; i++) {
        track->trains[i] = NULL;
    }
    track->num_trains = write_idx;

    return 0;
}


double get_track_occupancy(track_data_t *track) {
    if (track == NULL || track->length <= 0) {
        return 0.0;
    }

    double occupied_length = 0.0;

    for (int i = 0; i < track->num_trains && i < MAX_TRAINS; i++) {
        train_data_t *train = track->trains[i];
        if (train != NULL) {
            double front = train->front_position;
            double rear = train->rear_position;
            
            if (front > 0 && rear < (double)track->length) {
                double start = (rear < 0) ? 0 : rear;
                double end = (front > (double)track->length) ? 
                            (double)track->length : front;
                occupied_length += (end - start);
            }
        }
    }

    double occupancy_percent = (occupied_length / (double)track->length) * 100.0;
    
    if (occupancy_percent > 100.0) {
        occupancy_percent = 100.0;
    }
    if (occupancy_percent < 0.0) {
        occupancy_percent = 0.0;
    }

    return occupancy_percent;
}


double time_until_train_at_position(track_data_t *track, double position) {
    if (track == NULL || track->num_trains == 0) {
        return -1.0;
    }

    double min_time = -1.0;

    for (int i = 0; i < track->num_trains && i < MAX_TRAINS; i++) {
        train_data_t *train = track->trains[i];
        
        if (train == NULL || train->current_speed <= 0.0) {
            continue;  
        }

        double time_needed = (position - train->front_position) / train->current_speed;
        
        if (time_needed >= 0.0) {
            if (min_time < 0.0 || time_needed < min_time) {
                min_time = time_needed;
            }
        }
    }

    return min_time;
}


int compute_distance(train_data_t *train) {
    if (train == NULL) {
        return 0;
    }

    uint64_t current_time_ns = get_current_time_ns();
    if (current_time_ns < train->entry_time_ns) {
        return 0;
    }

    double elapsed_time = (double)(current_time_ns - train->entry_time_ns) / (double)NSEC_PER_SEC;

    double speed = (train->current_speed > 0.0) ? train->current_speed : (double)train->speed;
    double distance_traveled = calculate_distance_traveled(speed, elapsed_time);
    return (int)distance_traveled;
}

track_data_t update_track_data(track_data_t track_data, uint64_t current_time_ns) {
    
    for (int i = 0; i < track_data.num_trains && i < MAX_TRAINS; i++) {
        train_data_t *train = track_data.trains[i];
        if (train == NULL) {
            continue;
        }

        (void)update_train_position(train, current_time_ns);
    }
    
    return track_data;
}
