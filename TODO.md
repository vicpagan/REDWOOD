  - [VP]
  - Make sure that the ApplicationSpecs part works properly and updates with the rest of this list
  - Temporal redundancy hack
    - For chain tasks, keep track of previous exec option in the recursion
    - Probably easier to do a decision tree instead of a table
  - Cancel useless runs of tasks upon a single host completing hack
    - Need to make sure hosts that are down still get properly reset and wait
  - Change delta calculation to squeeze on expected error

BUGS:
  - Need to make sure hosts that are down still get properly reset and wait instead of skipping past the restart overhead