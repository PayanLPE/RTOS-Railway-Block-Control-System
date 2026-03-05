






# Components
## DeadlockManager (Server)
DeadlockManager acts like a server and handles requests from the clients (trains under `TrainController`) and prevents deadlocks from occuring through various physics computations regarding the trains data, such as speed and length, as well as the track data, such as track length and curvature.

## TrainController (Client)
Each train is given its own process and interacts through the TrainController to contact the DeadlockManager in order to be given information on whether or not to continue with the route or change speeds or routes.

## Common (Shared Library)
A shared library containing common data and functions between the TrainController and the DeadlockManager.



