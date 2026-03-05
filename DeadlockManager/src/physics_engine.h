#ifndef PHYSICS_ENGINE_H
#define PHYSICS_ENGINE_H

#include "ipc_protocol.h"
#include <time.h>

// ====================================================================================================
// Physics Engine Functions
// ====================================================================================================

/**
 * Initialize train position when it enters a track
 * @param train: Pointer to train_data_t structure
 * @param speed: Speed of the train in mm/s (use train->speed if not overriding)
 * @return 0 on success, -1 on failure
 */
int init_train_on_track(train_data_t *train, double speed);

/**
 * Calculate distance traveled by a train based on elapsed time and speed
 * @param speed: Speed of the train in mm/s
 * @param elapsed_time: Time elapsed since train entered track (seconds)
 * @return distance traveled in mm
 */
double calculate_distance_traveled(double speed, double elapsed_time);

/**
 * Update train position based on current time
 * Calculates front and rear positions based on speed and elapsed time
 * @param train: Pointer to train_data_t structure to update
 * @param current_time: Current time in seconds
 * @return 0 on success, -1 on failure
 */
int update_train_position(train_data_t *train, time_t current_time);

/**
 * Check if train has left the track (rear position >= track length)
 * @param train: Pointer to train data
 * @param track_length: Length of the track in mm
 * @return 1 if train has completely left the track, 0 otherwise
 */
int has_train_left_track(train_data_t *train, int track_length);

/**
 * Check if two trains on the same track are in collision
 * @param train1: First train data
 * @param train2: Second train data
 * @return 1 if trains collide (overlap), 0 otherwise
 */
int check_train_collision(train_data_t *train1, train_data_t *train2);

/**
 * Get the distance between two trains on the same track
 * @param train1: First train (ahead/behind)
 * @param train2: Second train (behind/ahead)
 * @return distance between rear of train1 and front of train2 (positive if separated)
 */
double get_distance_between_trains(train_data_t *train1, train_data_t *train2);

/**
 * Calculate time until train completely clears a track section
 * Section is defined by start and end positions
 * @param train: Train data
 * @param section_start: Start position of track section (mm)
 * @param section_end: End position of track section (mm)
 * @return time until train completely clears section (-1 if not applicable)
 */
double time_to_clear_section(train_data_t *train, 
                            double section_start, 
                            double section_end);

/**
 * Update all train positions on a track
 * @param track: Pointer to track_data_t structure
 * @param current_time: Current system time
 * @return 0 on success, -1 on failure
 */
int update_trains_on_track(track_data_t *track, time_t current_time);

/**
 * Get the occupancy percentage of a track
 * @param track: Pointer to track_data_t structure
 * @return percentage of track occupied by trains (0-100)
 */
double get_track_occupancy(track_data_t *track);

/**
 * Calculate when the next train will reach a specific position on track
 * @param track: Current track data
 * @param position: Position on track (mm from start)
 * @return time in seconds until a train reaches that position (-1 if none will)
 */
double time_until_train_at_position(track_data_t *track, double position);

/**
 * Compute the distance a train has traveled on its current track
 * Based on entry time, speed, and length
 * @param train: Pointer to train_data_t
 * @return distance traveled in mm (as integer)
 */
int compute_distance(train_data_t *train);

// Updates track data on every tick (train locations)
track_data_t update_track_data(track_data_t track_data);

#endif // PHYSICS_ENGINE_H
