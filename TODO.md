  - [VP]
  - Cancel useless runs of tasks upon a single host completing hack
    - Need to make sure hosts that are down still get properly reset and wait
    - Give option to disable this hack
  - Change delta calculation to squeeze on expected error
  - Calculate delta based on a single variable
    - By messing with the other variables, we can see how "sensitive" the calcation is to other variables
    - Try this with every independent variable
    - Eventually, we will have a variable that is the most sensitive, and we can use that for delta calculation
    - Or, consequently, we could possibly find a function that takes into account all variables and gives us a single sensitivity value
    - This would allow us to do delta calculation based on given constant values instead of doing heavy recalculations
    - Maybe if the graphs/results for a variable are nice enough, we can prove it theoretically
  - Make temporal redundancy viable with other heuristics
    - Right now, temporal redundancy always removes option combinations based on EV
    - Should work with static heuristics like:
      - Cheapest first (temp redundancy is based on prob of success and starts at the highest)
      - Expensive first (temp redundancy is based on error and starts at the lowest)
      - "Best" first (temp redundancy is based on EV and starts at the highest)
    - Static decision algorithm should also use decision tree and craft it based on whatever heuristic is being used
      - This would include changing the current implementation in Controller as well

BUGS:
  - Need to make sure hosts that are down still get properly reset and wait instead of skipping past the restart overhead

EXPERIMENTS:
    - Test cancel useless runs of tasks upon a single host completing hack
    - Test delta squeeze vs execution speed
    - Test one task vs chain?
    - Test basic heuristics on one host and one task/chain with temp redundancy
      - Graph would look like different colored sets of dots for each heuristic and best fit lines to compare them

NOTES:
    - With the stop_running_jobs hack, the difference in NodeKiller calls changes the timing of when hosts are killed
      - This means there's a difference between runs with and without the hack
      - Does this matter in the case of comparing the results of runs with and without the hack?
        - Probably not, since with enough runs the differences should average out