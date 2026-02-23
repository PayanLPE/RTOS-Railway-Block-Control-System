

Acts as a shared library for DeadlockManager and TrainController.
Under ./src/ipc_protocol.c are files that contain the agreement on the protocol for ipc between the DeadlockManager and the TrainController.


Things to agree on:
- format
- resource ids
- train ids
- command type (request, release, status)

ex:
typedef struct {
    int train_id;
    int request_type;   // REQUEST / RELEASE
    int resource_id;
} train_msg_t;