#ifndef RESOURCE_MANAGER_H
#define RESOURCE_MANAGER_H

#include <stdbool.h>
#include "ipc_protocol.h"

// Track allocation interface used by DeadlockManager message handlers.
void init_resource_manager();
bool request_track(train_data_t *train, int track_id);
void release_track(int train_id, int track_id);
void print_resource_status();

#endif
