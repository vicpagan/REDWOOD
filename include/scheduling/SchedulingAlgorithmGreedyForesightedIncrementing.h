#ifndef SCHEDULINGALGORITHMGREEDYFORESIGHTEDINCREMENTING_H
#define SCHEDULINGALGORITHMGREEDYFORESIGHTEDINCREMENTING_H

#include "SchedulingAlgorithmGreedyForesighted.h"

namespace wrench {
    /**
     * Greedy Foresight (Online Greedy):
     * Uses dynamic programming to consider future tasks when making decisions.
     * Preprocesses decisions by running DP for the entire task chain,
     * then extracts the first optimal decision for each task.
     */
    class SchedulingAlgorithmGreedyForesightedIncrementing : public SchedulingAlgorithmGreedyForesighted {
    public:
        SchedulingAlgorithmGreedyForesightedIncrementing(
            const std::shared_ptr<ApplicationSpecs>& application_specs,
            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
            ProbabilityComputation* probability_computation,
            OptionComparatorFunction* comparator_function)
            : SchedulingAlgorithmGreedyForesighted(application_specs, "greedy_foresighted_incrementing", exec_options,
                                       probability_computation, comparator_function) {
        }

        void initial_decisions(const std::string& hostname,
                                double initial_data_size,
                                 double initial_error_level,
                                 double deadline,
                                 bool lower_bound) override;

    };

}

#endif
