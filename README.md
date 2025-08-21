# REDWOOD

# Current Wrench Simulation Execution

```
cd build
cmake ..
make
./redwood_sim --json ../data/sample_input.json --wrench-host-shutdown-simulation --wrench-commport-pool-size=20000
```

### Execute with Logs

```
cd build
cmake ..
make
./redwood_sim --json ../data/sample_input.json --wrench-host-shutdown-simulation --wrench-commport-pool-size=20000 --wrench-full-logs
```
