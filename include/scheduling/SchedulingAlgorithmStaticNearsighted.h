#ifndef SCHEDULINGALGORITHMSTATICNEARSIGHTED_H
#define SCHEDULINGALGORITHMSTATICNEARSIGHTED_H

#include "SchedulingAlgorithmStatic.h"

namespace wrench {
    /**
     * Static Foresight (Online Static):
     * Uses dynamic programming to consider future tasks when making decisions.
     * Preprocesses decisions by running DP for the entire task chain,
     * then extracts the first optimal decision for each task.
     */
    class SchedulingAlgorithmStaticNearsighted : public SchedulingAlgorithmStatic {
    public:
        SchedulingAlgorithmStaticNearsighted(
            const std::shared_ptr<ApplicationSpecs>& application_specs,
            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
            ProbabilityComputation* probability_computation,
            OptionComparatorFunction* comparator_function)
            : SchedulingAlgorithmStatic(application_specs, "static_nearsighted", exec_options,
                                       probability_computation, comparator_function) {}

        void preprocess_decisions(double input_data_size,
                                 double input_error_level,
                                 double deadline,
                                 bool lower_bound) override;

        std::vector<SchedulingAlgorithm::SchedulingDecision> make_decisions(
            SystemState* system_state_tracker,
            const std::string& task_to_schedule,
            double input_data_size,
            double input_error_level,
            double remaining_time) override;
    };

}

#endif
