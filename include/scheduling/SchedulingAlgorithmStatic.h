#ifndef SCHEDULINGALGORITHMSTATIC_H
#define SCHEDULINGALGORITHMSTATIC_H

#include "SchedulingAlgorithm.h"

namespace wrench {
    /**
     * Static heuristics (Section V-D): each host is planned on its own, i.e., the greedy heuristics with N = 1
     * and without their initial decisions over all hosts.
     */
    class SchedulingAlgorithmStatic : public SchedulingAlgorithm {
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
        SchedulingAlgorithmStatic(
            const std::shared_ptr<ApplicationSpecs>& application_specs,
            const std::string& name,
            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
            Discretization* discretization,
            const std::string& criterion,
            const bool lower_bound)
            : SchedulingAlgorithm(application_specs, name, exec_options, discretization, lower_bound),
              _criterion(criterion) {}

        /**
         * @brief Plan the decisions of a host on its own (one host), storing them in _static_decisions_per_host
         * @param hostname The host
         * @param initial_data_size The input data size of the host's next task
         * @param initial_error_level The input error level of the host's next task
         * @param deadline The remaining time before the deadline
         * @param lower_bound Whether durations are discretized for the lower bound (instead of the upper bound)
         */
        void preprocess_host_decisions(const std::string& hostname,
            double initial_data_size,
            double initial_error_level,
            double deadline,
            bool lower_bound) override = 0;

        std::vector<SchedulingDecision> make_decisions(
            SystemState* system_state_tracker,
            double remaining_time) override;

        /**
         * @brief Discard the stored decisions of a host
         * @param hostname The host
         */
        void reset_host_preprocessed_decisions(const std::string& hostname) override {
            _static_decisions_per_host.erase(hostname);
        }

        /**
         * @brief Discard the stored decisions of all hosts
         */
        void reset_all_preprocessed_decisions() override {
            _static_decisions_per_host.clear();
            _expected_error = 0.0;
        }

    protected:
        std::string _criterion;
        std::map<std::string, std::map<std::string, std::string>> _static_decisions_per_host;
        double _expected_error = 0.0;
    };
}

#endif //SCHEDULINGALGORITHMSTATIC_H
