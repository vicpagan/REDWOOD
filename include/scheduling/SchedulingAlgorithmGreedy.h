#ifndef SCHEDULINGALGORITHMGREEDY_H
#define SCHEDULINGALGORITHMGREEDY_H

#include "SchedulingAlgorithm.h"

namespace wrench {
    /**
     * Greedy heuristics (Section VI-A): the first decisions spread the N hosts over options (initial_decisions());
     * after that, each host is planned on its own, as with the static heuristics (preprocess_host_decisions()).
     */
    class SchedulingAlgorithmGreedy : public SchedulingAlgorithm {
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
        SchedulingAlgorithmGreedy(
            const std::shared_ptr<ApplicationSpecs>& application_specs,
            const std::string& name,
            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
            Discretization* discretization,
            const std::string& criterion,
            const bool lower_bound)
            : SchedulingAlgorithm(application_specs, name, exec_options, discretization, lower_bound),
              _criterion(criterion),
              _num_compute_nodes(application_specs->get_num_compute_nodes()) {
        }

        /**
         * @brief Plan the decisions of a host on its own (one host), after the initial decisions, storing them
         *        in _static_decisions_per_host
         * @param hostname The host
         * @param initial_data_size The input data size of the host's next task
         * @param initial_error_level The input error level of the host's next task
         * @param remaining_time The remaining time before the deadline
         * @param lower_bound Whether durations are discretized for the lower bound (instead of the upper bound)
         */
        void preprocess_host_decisions(const std::string& hostname,
            double initial_data_size,
            double initial_error_level,
            double remaining_time,
            bool lower_bound) override = 0;

        /**
         * @brief First decisions (greedy only): decide how many hosts execute each option (nearsighted) or each
         *        sequence of options (foresighted), from the first task
         * @param hostname The host whose current decision node is used (the first host)
         * @param initial_data_size The application's initial data size
         * @param initial_error_level The application's initial error level
         * @param deadline The remaining time before the deadline
         * @param lower_bound Whether durations are discretized for the lower bound (instead of the upper bound)
         */
        virtual void initial_decisions(const std::string& hostname,
                                 double initial_data_size,
                                 double initial_error_level,
                                 double deadline,
                                 bool lower_bound) = 0;

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
         * @brief Discard the stored decisions of all hosts (the next make_decisions() call makes the initial
         *        decisions again)
         */
        void reset_all_preprocessed_decisions() override {
            _static_decisions_per_host.clear();
            _expected_error = 0.0;
        }

    protected:

        /**
         * @brief Turn the numbers of hosts decided by initial_decisions() into per-host decisions in
         *        _static_decisions_per_host, assigning hosts in the system state's order
         * @param system_state_tracker The state of the hosts
         */
        virtual void translate_to_static_decisions(SystemState* system_state_tracker) = 0;

        std::map<std::string, std::map<std::string, std::string>> _static_decisions_per_host;

        std::string _criterion;
        long _num_compute_nodes;
        double _expected_error = 0.0;
    };
}

#endif //SCHEDULINGALGORITHMGREEDY_H