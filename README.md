# REDWOOD

# Current Wrench Simulation Execution

```
cd build
cmake ..
make
./redwood_sim --json ../data/sample_input.json --wrench-host-shutdown-simulation --wrench-commport-pool-size=20000
```

# Old Main File Execution

## Usage
``` 
$ make
$ ./main <lambda> <restart overhead> <task time> <time to deadline>
```

## Example
``` 
$ ./main 0.001 50 1000 5000
```
This runs an experiment for a single task with an execution time of `1000ms`, a deadline for the task to finish of `5000ms`, with a lambda failure rate of `0.001` and a restart overhead of `50ms`.  

It compares average execution time via speedup constant, and accuracy using a relative epsilon.
