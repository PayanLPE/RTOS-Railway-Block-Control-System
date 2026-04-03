# How to Build and Run

## Compile

```bash
cd DeadlockManager && make
cd ../TrainController && make
```

---

## Run

**DeadlockManager must be started first.**

**Terminal 1 — server:**
```bash
./DeadlockManager/build/x86_64-debug/DeadlockManager <track_config>
```

**Terminal 2 — clients:**
```bash
./TrainController/build/x86_64-debug/TrainController <train_config>
```

### Example scenarios (`examples/`)

| Track config | Train config | Description |
|---|---|---|
| `track_config_example.txt` | `train_config_example.txt` | Basic routing |
| `track_config_demo.txt` | `train_config_demo.txt` | Resolvable deadlock cycle |
| `track_config_deadlock_cycle.txt` | `train_config_deadlock_cycle.txt` | Deadlock recovery test |

TrainController exits when all trains finish. Stop DeadlockManager with `Ctrl+C`.
