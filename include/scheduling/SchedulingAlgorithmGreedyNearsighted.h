#ifndef SCHEDULINGALGORITHMGREEDYNEARSIGHTED_H
#define SCHEDULINGALGORITHMGREEDYNEARSIGHTED_H

#include "SchedulingAlgorithmGreedy.h"

namespace wrench {
    /**
     * Greedy Nearsighted:
     * Initially spreads the hosts over the options of the first task according to the criterion,
     * assuming that it is the last task in the chain. After that, each host chooses the best option
     * of its next task on its own (as Static Nearsighted).
     */
    class SchedulingAlgorithmGreedyNearsighted : public SchedulingAlgorithmGreedy {
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
        SchedulingAlgorithmGreedyNearsighted(
            const std::shared_ptr<ApplicationSpecs>& application_specs,
            const std::string& name,
            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
            Discretization* discretization,
            const std::string& criterion,
            const bool lower_bound)
            : SchedulingAlgorithmGreedy(application_specs, name, exec_options,
                                       discretization, criterion, lower_bound) {
        }

        void initial_decisions(const std::string& hostname,
                                 double initial_data_size,
                                 double initial_error_level,
                                 double deadline,
                                 bool lower_bound) override;

        void preprocess_host_decisions(const std::string& hostname,
                                 double initial_data_size,
                                 double initial_error_level,
                                 double deadline,
                                 bool lower_bound) override;

        /**
         * @brief Discard the stored decisions of a host, and the numbers of hosts from the initial decisions
         *        (for all hosts)
         * @param hostname The host
         */
        void reset_host_preprocessed_decisions(const std::string& hostname) override {
            SchedulingAlgorithmGreedy::reset_host_preprocessed_decisions(hostname);
            _nodes_per_initial_option_decision.clear();
        }

        /**
         * @brief Discard the stored decisions of all hosts and the numbers of hosts from the initial decisions
         */
        void reset_all_preprocessed_decisions() override {
            SchedulingAlgorithmGreedy::reset_all_preprocessed_decisions();
            _nodes_per_initial_option_decision.clear();
        }

    protected:

        std::map<std::string, long> _nodes_per_initial_option_decision;

        void translate_to_static_decisions(SystemState* system_state_tracker) override;

    };

}

#endif
