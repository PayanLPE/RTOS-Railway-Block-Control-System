#include "deadlock_detector.h"
#include "physics_engine.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define STUCK_TIMEOUT_NS 5000000000LL  // 5 seconds

// DFS-based cycle detection for resource allocation graph
typedef enum {
    WHITE,      // Not visited
    GRAY,       // Currently visiting
    BLACK       // Finished visiting
} dfs_color_t;

typedef struct {
    int node_id;          // Train ID or Track ID (tracks have negative IDs: -track_id-1)
    int node_type;        // 0 = train, 1 = track
    dfs_color_t color;
    int in_degree;
    int out_degree;
} graph_node_t;

typedef struct {
    graph_node_t nodes[MAX_TRAINS * 2 + MAX_TRACKS];
    int node_count;
    
    // Adjacency list: from[i] = list of nodes that node i points to
    int to[MAX_TRAINS * 2 + MAX_TRACKS][MAX_TRAINS + MAX_TRACKS];
    int to_count[MAX_TRAINS * 2 + MAX_TRACKS];
} resource_graph_t;

void initialize_train_request_state(train_request_state_t *state, int train_id) {
    if (state == NULL) return;
    
    state->train_id = train_id;
    state->state = TRAIN_ACTIVE;
    state->currently_holds_track = -1;
    state->currently_wants_track = -1;
    state->request_time_ns = 0;
    state->retry_count = 0;
}

void update_train_request_state(train_request_state_t *state,
                               int holds_track, int wants_track,
                               uint64_t current_time_ns) {
    if (state == NULL) return;

    int previous_wants_track = state->currently_wants_track;
    state->currently_holds_track = holds_track;
    state->currently_wants_track = wants_track;

    if (wants_track != -1 && wants_track != previous_wants_track) {
        state->request_time_ns = current_time_ns;
        state->retry_count = 0;
    }
}

int detect_stuck_train(train_request_state_t *train_state, uint64_t current_time_ns) {
    if (train_state == NULL || train_state->currently_wants_track == -1) {
        return 0;  // Not stuck: not waiting for anything
    }
    
    if (train_state->request_time_ns == 0) {
        return 0;  // First time requesting
    }
    
    uint64_t wait_time_ns = current_time_ns - train_state->request_time_ns;
    
    if (wait_time_ns > STUCK_TIMEOUT_NS) {
        train_state->state = TRAIN_STUCK;
        return 1;  // Train is stuck
    }
    
    train_state->state = TRAIN_WAITING;
    return 0;
}

// Build the resource allocation graph from current train/track state
static void build_resource_graph(train_request_state_t *trains, int train_count,
                                 track_data_t *tracks, int track_count,
                                 resource_graph_t *graph) {
    if (graph == NULL) return;
    
    memset(graph, 0, sizeof(resource_graph_t));
    
    // Add all train nodes and track nodes
    for (int i = 0; i < train_count; i++) {
        if (trains[i].train_id >= 0) {
            graph->nodes[graph->node_count].node_id = trains[i].train_id;
            graph->nodes[graph->node_count].node_type = 0;  // train
            graph->nodes[graph->node_count].color = WHITE;
            graph->node_count++;
        }
    }
    
    for (int i = 0; i < track_count; i++) {
        if (tracks[i].track_id >= 0) {
            graph->nodes[graph->node_count].node_id = tracks[i].track_id;
            graph->nodes[graph->node_count].node_type = 1;  // track
            graph->nodes[graph->node_count].color = WHITE;
            graph->node_count++;
        }
    }
    
    // Build edges based on resource allocation
    // Train -> Track edges: train holds this track, train wants this track
    for (int i = 0; i < train_count; i++) {
        if (trains[i].train_id < 0) continue;
        
        int train_idx = i;
        
        // Edge: Train holds Track (train -> track)
        if (trains[i].currently_holds_track >= 0) {
            for (int j = 0; j < graph->node_count; j++) {
                if (graph->nodes[j].node_type == 1 && 
                    graph->nodes[j].node_id == trains[i].currently_holds_track) {
                    graph->to[train_idx][graph->to_count[train_idx]++] = j;
                    graph->nodes[j].in_degree++;
                    break;
                }
            }
        }
        
        // Edge: Track -> Train (if train waiting for track, track blocks train)
        if (trains[i].currently_wants_track >= 0) {
            for (int j = 0; j < graph->node_count; j++) {
                if (graph->nodes[j].node_type == 1 && 
                    graph->nodes[j].node_id == trains[i].currently_wants_track) {
                    graph->to[j][graph->to_count[j]++] = train_idx;
                    graph->nodes[train_idx].in_degree++;
                    break;
                }
            }
        }
    }
}

// DFS to detect cycles in resource allocation graph
// Returns the node_id of a train in a cycle, or -1 if no cycle
static int dfs_detect_cycle(resource_graph_t *graph, int node_idx,
                            int *cycle_train_id) {
    if (graph->nodes[node_idx].color == GRAY) {
        // Back edge found - cycle detected
        if (graph->nodes[node_idx].node_type == 0) {
            *cycle_train_id = graph->nodes[node_idx].node_id;
        }
        return 1;
    }
    
    if (graph->nodes[node_idx].color == BLACK) {
        return 0;  // Already fully explored
    }
    
    graph->nodes[node_idx].color = GRAY;
    
    for (int i = 0; i < graph->to_count[node_idx]; i++) {
        int next = graph->to[node_idx][i];
        if (dfs_detect_cycle(graph, next, cycle_train_id)) {
            return 1;
        }
    }
    
    graph->nodes[node_idx].color = BLACK;
    return 0;
}

int detect_deadlock(train_request_state_t *trains, int train_count,
                   track_data_t *tracks, int track_count) {
    if (trains == NULL || tracks == NULL || train_count <= 0) {
        return -1;
    }
    
    resource_graph_t graph;
    build_resource_graph(trains, train_count, tracks, track_count, &graph);
    
    // Check for cycles in the graph
    for (int i = 0; i < graph.node_count; i++) {
        if (graph.nodes[i].color == WHITE && graph.nodes[i].node_type == 0) {
            int cycle_train = -1;
            if (dfs_detect_cycle(&graph, i, &cycle_train)) {
                printf("[DEADLOCK] Cycle detected involving train %d\n", cycle_train);
                return cycle_train;  // Return first train in cycle
            }
        }
    }
    
    return -1;  // No deadlock
}

int suggest_reroute(int blocked_train_id, train_data_t *trains, int train_count,
                   track_data_t *tracks, int track_count,
                   int current_destination, int *suggested_track) {
    if (trains == NULL || tracks == NULL || suggested_track == NULL) {
        return -1;
    }
    
    // Block model: prefer any free track that isn't current destination.
    int best_track = -1;

    for (int i = 0; i < track_count; i++) {
        if (tracks[i].track_id < 0 || tracks[i].track_id == current_destination) {
            continue;  // Skip invalid or current destination
        }

        if (tracks[i].num_trains == 0) {
            best_track = tracks[i].track_id;
            break;
        }
    }
    
    if (best_track >= 0) {
        *suggested_track = best_track;
        printf("[REROUTE] train=%d suggest track=%d\n", blocked_train_id, best_track);
        return 0;
    }
    
    return -1;  // No suitable alternate track found
}

void print_deadlock_graph(train_request_state_t *trains, int train_count) {
    printf("\n[RESOURCE GRAPH STATUS]\n");
    printf("Train Request States:\n");
    
    for (int i = 0; i < train_count; i++) {
        if (trains[i].train_id < 0) continue;
        
        printf("  Train %d: ", trains[i].train_id);
        
        if (trains[i].currently_holds_track >= 0) {
            printf("holds Track %d", trains[i].currently_holds_track);
        } else {
            printf("holds nothing");
        }
        
        if (trains[i].currently_wants_track >= 0) {
            printf(", wants Track %d", trains[i].currently_wants_track);
        }
        
        switch (trains[i].state) {
            case TRAIN_ACTIVE:
                printf(" [ACTIVE]\n");
                break;
            case TRAIN_WAITING:
                printf(" [WAITING]\n");
                break;
            case TRAIN_STUCK:
                printf(" [STUCK]\n");
                break;
        }
    }
    printf("\n");
}
