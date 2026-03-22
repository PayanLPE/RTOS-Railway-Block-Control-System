#ifndef PHYSICS_ENGINE_H
#define PHYSICS_ENGINE_H

#include "ipc_protocol.h"
#include <time.h>

// Core kinematics helpers.

int init_train_on_track(train_data_t *train, double speed);

uint64_t get_current_time_ns(void);


double calculate_distance_traveled(double speed, double elapsed_time);


int update_train_position(train_data_t *train, uint64_t current_time_ns);


int has_train_left_track(train_data_t *train, int track_length);


int check_train_collision(train_data_t *train1, train_data_t *train2);


double get_distance_between_trains(train_data_t *train1, train_data_t *train2);


// Track-level aggregate helpers.
double time_to_clear_section(train_data_t *train, 
                            double section_start, 
                            double section_end);


int update_trains_on_track(track_data_t *track, uint64_t current_time_ns);


double get_track_occupancy(track_data_t *track);


double time_until_train_at_position(track_data_t *track, double position);


int compute_distance(train_data_t *train);

track_data_t update_track_data(track_data_t track_data, uint64_t current_time_ns);

#endif
