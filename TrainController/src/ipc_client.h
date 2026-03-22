#ifndef IPC_CLIENT_H
#define IPC_CLIENT_H

#include <stdbool.h>

// Thin wrapper around DeadlockManager request/release IPC calls.
bool request_track_from_manager(int train_id, int track_id);
bool release_track_to_manager(int train_id, int track_id);

#endif
