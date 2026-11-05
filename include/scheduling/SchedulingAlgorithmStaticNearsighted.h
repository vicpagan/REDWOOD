#ifndef SCHEDULINGALGORITHMSTATICNEARSIGHTED_H
#define SCHEDULINGALGORITHMSTATICNEARSIGHTED_H

#include "SchedulingAlgorithmStatic.h"

namespace wrench {
    /**
     * Static Nearsighted:
     * For each host, chooses the best option of its next task according to the criterion,
     * assuming that this task is the last task in the chain.
     */
    class SchedulingAlgorithmStaticNearsighted : public SchedulingAlgorithmStatic {
    public:
        /**
         * @brief Constructor
         * @param application_specs The application specification
         * @param name The algorithm's name
         * @param exec_options The option functions of each option of each task
         * @param discretization The discretization
         * @param criterion The criterion: expected_error, probability_success, success_error_ratio or error_level
         * @param lower_bound Whether durations are discretized for the lower bound (instead of the upper bound)
         */
        SchedulingAlgorithmStaticNearsighted(
            const std::shared_ptr<ApplicationSpecs>& application_specs,
            const std::string& name,
            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
            Discretization* discretization,
            const std::string& criterion,
            const bool lower_bound)
            : SchedulingAlgorithmStatic(application_specs, name, exec_options,
                                       discretization, criterion, lower_bound) {}

        void preprocess_host_decisions(const std::string& hostname,
                                 double initial_data_size,
                                 double initial_error_level,
                                 double deadline,
                                 bool lower_bound) override;

    };

}

#endif
