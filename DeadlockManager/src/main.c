#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <sys/neutrino.h>
#include <sys/dispatch.h>
#include <sys/iofunc.h>
#include <sys/netmgr.h>

#include "ipc_protocol.h"
#include "resource_manager.h"
#include "physics_engine.h"
#include "deadlock_detector.h"

#define DEADLOCK_MANAGER_NAME "DeadlockManager"

#define CONFIG_LINE_MAX 256
#define TICK_INTERVAL_NS 10000000LL
#define TICK_PULSE_CODE (_PULSE_CODE_MINAVAIL + 1)
#define DEADLOCK_LOG_INTERVAL_NS 1000000000ULL
#define DEFAULT_BLOCK_LENGTH_MM 1000

typedef union {
    struct _pulse pulse;
    ipc_message_t msg;
} deadlock_receive_t;

// Global track registry loaded from config at startup.
track_data_t track_list[MAX_TRACKS];
int track_count = 0;

// Cached train data fetched on demand from TrainController.
train_data_t active_trains[MAX_TRAINS];
int active_train_count = 0;

// Train request states for deadlock detection
train_request_state_t train_request_states[MAX_TRAINS];
static uint64_t last_deadlock_log_ns = 0;
static int last_deadlock_train = -1;

// Guards shared track/train state across message and timer paths.
pthread_mutex_t track_mutex = PTHREAD_MUTEX_INITIALIZER;

#define TRAIN_CONTROLLER_NAME "TrainController"

static int train_is_on_any_track(int train_id) {
    for (int i = 0; i < track_count; i++) {
        if (track_list[i].track_id < 0) {
            continue;
        }

        for (int j = 0; j < track_list[i].num_trains; j++) {
            train_data_t *t = track_list[i].trains[j];
            if (t != NULL && t->train_id == train_id) {
                return 1;
            }
        }
    }

    return 0;
}

train_data_t *get_or_create_train_data(int train_id) {
    for (int i = 0; i < active_train_count; i++) {
        if (active_trains[i].train_id == train_id) {
            return &active_trains[i];
        }
    }

    if (active_train_count >= MAX_TRAINS) {
        printf("Maximum active train count reached\n");
        return NULL;
    }

    int coid = name_open(TRAIN_CONTROLLER_NAME, 0);
    if (coid == -1) {
        printf("Failed to connect to TrainController for train %d: %s\n", train_id, strerror(errno));
        return NULL;
    }

    train_query_message_t msg;
    msg.type = MSG_GET_TRAIN_DATA;
    msg.train_id = train_id;

    train_query_message_t reply;

    if (MsgSend(coid, &msg, sizeof(msg), &reply, sizeof(reply)) == -1) {
        printf("Failed to query train data for train %d\n", train_id);
        ConnectDetach(coid);
        return NULL;
    }

    ConnectDetach(coid);

    if (reply.type == MSG_TRAIN_DATA_REPLY) {
        active_trains[active_train_count] = reply.train_data;
        active_train_count++;
        return &active_trains[active_train_count - 1];
    } else {
        printf("TrainController denied train data request for train %d\n", train_id);
        return NULL;
    }
}

void send_position_updates(uint64_t tick_time_ns, unsigned long long tick_count) {
    static int missing_update_warned[MAX_TRAINS] = {0};

    for (int i = 0; i < active_train_count; i++) {
        train_data_t *train = &active_trains[i];

        if (train->track_id < 0 || train->track_id >= MAX_TRACKS) {
            continue;
        }

        // Each train listens on a per-train named channel (Train<id>).
        char train_name[32];
        sprintf(train_name, "Train%d", train->train_id);
        int train_coid = name_open(train_name, 0);
        if (train_coid == -1) {
            if (!missing_update_warned[i]) {
                printf("[WARN] unable to connect for updates: train=%d reason=%s\n", train->train_id, strerror(errno));
                missing_update_warned[i] = 1;
            }
            continue;
        }
        missing_update_warned[i] = 0;

        position_update_message_t update_msg;
        update_msg.type = MSG_POSITION_UPDATE;
        update_msg.train_id = train->train_id;
        update_msg.track_id = train->track_id;
        update_msg.track_length = track_list[train->track_id].length;
        update_msg.front_position = train->front_position;
        update_msg.rear_position = train->rear_position;
        update_msg.current_speed = train->current_speed;
        update_msg.tick_time_ns = tick_time_ns;
        update_msg.tick_count = tick_count;

        if (MsgSend(train_coid, &update_msg, sizeof(update_msg), NULL, 0) == -1) {
            printf("Failed to send position update to train %d: %s\n", train->train_id, strerror(errno));
        }

        ConnectDetach(train_coid);
    }
}

int load_track_data(const char *filename) {
    printf("Loading track data from: %s\n", filename);
    
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Failed to open track file");
        return 0;
    }

    char line[CONFIG_LINE_MAX];
    int line_num = 0;
    while (fgets(line, sizeof(line), file)) {
        line_num++;
        
        if (line[0] == '#' || strlen(line) < 3) {
            printf("  Line %d: skipped (comment or empty)\n", line_num);
            continue;
        }

        track_data_t track;
        memset(&track, 0, sizeof(track_data_t));

        int parsed = sscanf(line, "%d %d %d %d %d %d",
                            &track.track_id,
                            &track.direction,
                            &track.endpoints[0],
                            &track.endpoints[1],
                            &track.endpoints[2],
                            &track.endpoints[3]);

        if (parsed == 6) {
            track.length = DEFAULT_BLOCK_LENGTH_MM;
        } else {
            parsed = sscanf(line, "%d %d %d %d %d %d %d",
                            &track.track_id,
                            &track.length,
                            &track.direction,
                            &track.endpoints[0],
                            &track.endpoints[1],
                            &track.endpoints[2],
                            &track.endpoints[3]);
            if (parsed != 7) {
                printf("  Line %d: INVALID format: %s", line_num, line);
                continue;
            }
        }

        memset(track.trains, 0, sizeof(track.trains));
        track.num_trains = 0;

        if (track.track_id < 0 || track.track_id >= MAX_TRACKS) {
            printf("  Line %d: INVALID track_id %d (must be 0-%d)\n", line_num, track.track_id, MAX_TRACKS-1);
            continue;
        }

        track_list[track.track_id] = track;
        track_count++;
         printf("  Line %d: ✓ Loaded track %d (direction=%d, endpoints=[%d,%d,%d,%d])\n",
             line_num, track.track_id, track.direction,
               track.endpoints[0], track.endpoints[1], track.endpoints[2], track.endpoints[3]);
    }

    fclose(file);
    printf("Track loading complete: %d tracks loaded\n\n", track_count);
    return 1;
}

void check_deadlock_and_recover(uint64_t current_time_ns) {
    // Update train request states based on current track occupancy
    for (int i = 0; i < active_train_count; i++) {
        train_data_t *train = &active_trains[i];
        
        int holds_track = train->track_id;
        int wants_track = train_request_states[i].currently_wants_track;
        if (holds_track < 0) {
            wants_track = -1;
        }
        
        update_train_request_state(&train_request_states[i], holds_track, wants_track, current_time_ns);
        
        // Check if train is stuck
        if (detect_stuck_train(&train_request_states[i], current_time_ns)) {
            printf("[STUCK] train=%d waiting>5s\n", train->train_id);
            
            // Block-model recovery planning: suggest alternate free block.
            int suggested_track = -1;
            if (suggest_reroute(train->train_id, active_trains, active_train_count,
                               track_list, track_count, train->track_id, &suggested_track) == 0) {
                printf("[PLAN] train=%d consider-reroute track=%d\n", train->train_id, suggested_track);
            }
        }
    }
    
    // Detect deadlocks
    int deadlock_train = detect_deadlock(train_request_states, active_train_count,
                                         track_list, track_count);
    
    if (deadlock_train >= 0) {
        if (deadlock_train != last_deadlock_train ||
            current_time_ns - last_deadlock_log_ns >= DEADLOCK_LOG_INTERVAL_NS) {
            printf("[DEADLOCK DETECTED] train=%d\n", deadlock_train);
            print_deadlock_graph(train_request_states, active_train_count);
            last_deadlock_log_ns = current_time_ns;
            last_deadlock_train = deadlock_train;
        }
        
        // Try to recover by suggesting reroute
        for (int i = 0; i < active_train_count; i++) {
            if (active_trains[i].train_id == deadlock_train) {
                int suggested_track = -1;
                if (suggest_reroute(deadlock_train, active_trains, active_train_count,
                                   track_list, track_count, active_trains[i].track_id, 
                                   &suggested_track) == 0) {
                          printf("[RECOVERY] suggest train=%d reroute_track=%d\n",
                              deadlock_train, suggested_track);
                }
                break;
            }
        }
    }
}


int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <track_config_file>\n", argv[0]);
        return 1;
    }

    int chid; 
    int rcvid; 
    deadlock_receive_t recv; 

    if (!load_track_data(argv[1])) {
        return 1;
    }

    // Initialize train request states
    for (int i = 0; i < MAX_TRAINS; i++) {
        initialize_train_request_state(&train_request_states[i], -1);
    }

    init_resource_manager();

    name_attach_t *attach = name_attach(NULL, DEADLOCK_MANAGER_NAME, 0);
    if (attach == NULL) {
        perror("name_attach failed");
        return 1;
    }
    chid = attach->chid;

    int tick_coid = ConnectAttach(ND_LOCAL_NODE, 0, chid, _NTO_SIDE_CHANNEL, 0);
    if (tick_coid == -1) {
        perror("ConnectAttach failed");
        name_detach(attach, 0);
        return 1;
    }

    struct sigevent tick_event;
    SIGEV_PULSE_INIT(&tick_event, tick_coid, SIGEV_PULSE_PRIO_INHERIT, TICK_PULSE_CODE, 0);

    timer_t tick_timer;
    if (timer_create(CLOCK_MONOTONIC, &tick_event, &tick_timer) == -1) {
        perror("timer_create failed");
        ConnectDetach(tick_coid);
        name_detach(attach, 0);
        return 1;
    }

    struct itimerspec tick_spec;
    memset(&tick_spec, 0, sizeof(tick_spec));
    tick_spec.it_value.tv_nsec = TICK_INTERVAL_NS;
    tick_spec.it_interval.tv_nsec = TICK_INTERVAL_NS;
    if (timer_settime(tick_timer, 0, &tick_spec, NULL) == -1) {
        perror("timer_settime failed");
        timer_delete(tick_timer);
        ConnectDetach(tick_coid);
        name_detach(attach, 0);
        return 1;
    }

    printf("\n[INFO] DeadlockManager running pid=%d chid=%d name=%s\n\n", getpid(), chid, DEADLOCK_MANAGER_NAME);

    // Timer pulses drive physics ticks; regular messages handle track requests.
    unsigned long long tick_count = 0;
    while (1) {
        rcvid = MsgReceive(chid, &recv, sizeof(recv), NULL);
        if (rcvid == -1) {
            continue;
        }

        if (rcvid == 0) {
            // Pulse path: periodic tick or disconnect notification.
            switch (recv.pulse.code) {
                case TICK_PULSE_CODE: {
                    uint64_t current_time_ns = get_current_time_ns();

                    tick_count++;

                    pthread_mutex_lock(&track_mutex);
                    for (int i = 0; i < track_count; i++) {
                        if (track_list[i].track_id >= 0) {
                            track_list[i] = update_track_data(track_list[i], current_time_ns);
                        }
                    }
                    send_position_updates(current_time_ns, tick_count);
                    
                    // Check for deadlocks and stuck trains every 10 ticks
                    if (tick_count % 10 == 0) {
                        check_deadlock_and_recover(current_time_ns);
                    }
                    
                    pthread_mutex_unlock(&track_mutex);
                    break;
                }
                case _PULSE_CODE_DISCONNECT:
                    ConnectDetach(recv.pulse.scoid);
                    break;
                default:
                    break;
            }

            continue;
        }

        // Message path: request/release from trains.
        ipc_message_t reply;

        reply.type = MSG_DENY;
        reply.train_id = recv.msg.train_id;
        reply.track_id = recv.msg.track_id;

        pthread_mutex_lock(&track_mutex);
        
        uint64_t current_time_ns = get_current_time_ns();

        switch (recv.msg.type) {
            case MSG_REQUEST_TRACK: {
                train_data_t *train = get_or_create_train_data(recv.msg.train_id);

                if (train != NULL) {
                    for (int i = 0; i < active_train_count; i++) {
                        if (active_trains[i].train_id == recv.msg.train_id &&
                            train_request_states[i].train_id < 0) {
                            initialize_train_request_state(&train_request_states[i], recv.msg.train_id);
                            break;
                        }
                    }
                }
                
                // Update train request state (now wants this track)
                for (int i = 0; i < active_train_count; i++) {
                    if (active_trains[i].train_id == recv.msg.train_id) {
                        update_train_request_state(&train_request_states[i], 
                                                   active_trains[i].track_id, 
                                                   recv.msg.track_id, 
                                                   current_time_ns);
                        break;
                    }
                }
                
                if (train == NULL) {
                    reply.type = MSG_DENY;
                    printf("[DENY] train=%d track=%d reason=no-train-data\n", recv.msg.train_id, recv.msg.track_id);
                } else if (request_track(train, recv.msg.track_id)) {
                    track_data_t *track = &track_list[recv.msg.track_id];
                    int old_track_id = train->track_id;
                    if (old_track_id != -1 && old_track_id != recv.msg.track_id) {
                        release_track(recv.msg.train_id, old_track_id);
                        printf("[RELEASE] train=%d track=%d\n", recv.msg.train_id, old_track_id);
                    }

                    // Block model: exactly one train per track.
                    memset(track->trains, 0, sizeof(track->trains));
                    track->trains[0] = train;
                    track->num_trains = 1;

                    train->track_id = recv.msg.track_id;

                    // Update state: now holds this track
                    for (int i = 0; i < active_train_count; i++) {
                        if (active_trains[i].train_id == recv.msg.train_id) {
                            update_train_request_state(&train_request_states[i],
                                                       recv.msg.track_id,
                                                       -1,
                                                       current_time_ns);
                            break;
                        }
                    }

                    reply.type = MSG_ACK;
                    printf("[GRANT] train=%d track=%d\n", recv.msg.train_id, recv.msg.track_id);
                } else {
                    reply.type = MSG_DENY;
                    printf("[DENY] train=%d track=%d\n", recv.msg.train_id, recv.msg.track_id);

                    // If this train holds no track, it likely failed initial admission.
                    // Mark it inactive so tick path stops trying position updates for it.
                    if (train != NULL && !train_is_on_any_track(train->train_id)) {
                        train->track_id = -1;
                    }
                }
                break;
            }
            case MSG_RELEASE_TRACK: {
                release_track(recv.msg.train_id, recv.msg.track_id);
                
                // Update train request state: no longer holds this track
                for (int i = 0; i < active_train_count; i++) {
                    if (active_trains[i].train_id == recv.msg.train_id) {
                        update_train_request_state(&train_request_states[i], 
                                                   -1, 
                                                   -1, 
                                                   current_time_ns);
                        break;
                    }
                }
                
                reply.type = MSG_ACK;
                printf("[RELEASE] train=%d track=%d\n", recv.msg.train_id, recv.msg.track_id);
                break;
            }
            default:
                printf("Unknown message type from Train %d\n", recv.msg.train_id);
                break;
        }

        print_resource_status();

        pthread_mutex_unlock(&track_mutex);

        MsgReply(rcvid, 0, &reply, sizeof(reply));
    }

    timer_delete(tick_timer);
    ConnectDetach(tick_coid);
    name_detach(attach, 0);
    return 0;
}