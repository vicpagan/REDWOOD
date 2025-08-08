# REDWOOD

# Usage
``` 
$ make
$ ./main <t> <d> <delta multiplier>
```
Where `t` is the execution time of the task, `d` is the deadline for the task, and `delta multiplier` is a factor to scale the delta value for testing.

# Example
``` 
$ ./main 1000 5000 2
```
This runs an experiment for a single task with an execution time of 1000ms, a deadline for the task to finish of 5000ms, and a multiplier to test against the default delta of 2.  
For example, if the default delta is 0.5, this will test the same task with a delta of 0.5ms and a delta of 1ms (0.5 * 2) and compare them.
