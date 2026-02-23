#ifndef RESOURCE_MANAGER_H
#define RESOURCE_MANAGER_H

#include <stdbool.h>

void init_resource_manager();
bool request_track(int train_id, int track_id);
void release_track(int train_id, int track_id);
void print_resource_status();

#endif // RESOURCE_MANAGER_H
