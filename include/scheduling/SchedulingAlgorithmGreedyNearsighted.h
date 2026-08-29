#ifndef SCHEDULINGALGORITHMGREEDYNEARSIGHTED_H
#define SCHEDULINGALGORITHMGREEDYNEARSIGHTED_H

#include "SchedulingAlgorithmGreedy.h"

namespace wrench {
    /**
     * Greedy Foresight (Online Greedy):
     * Uses dynamic programming to consider future tasks when making decisions.
     * Preprocesses decisions by running DP for the entire task chain,
     * then extracts the first optimal decision for each task.
     */
    class SchedulingAlgorithmGreedyNearsighted : public SchedulingAlgorithmGreedy {
    public:
        SchedulingAlgorithmGreedyNearsighted(
            const std::shared_ptr<ApplicationSpecs>& application_specs,
            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
            ProbabilityComputation* probability_computation,
            OptionComparatorFunction* comparator_function)
            : SchedulingAlgorithmGreedy(application_specs, "greedy_nearsighted", exec_options,
                                       probability_computation, comparator_function) {
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

        void reset_host_preprocessed_decisions(const std::string& hostname) override {
            SchedulingAlgorithmGreedy::reset_host_preprocessed_decisions(hostname);
            _nodes_per_initial_option_decision.clear();
            _list_of_options.clear();
        }

        void reset_all_preprocessed_decisions() override {
            SchedulingAlgorithmGreedy::reset_all_preprocessed_decisions();
            _nodes_per_initial_option_decision.clear();
            _list_of_options.clear();
        }

    protected:

        std::map<std::string, long> _nodes_per_initial_option_decision;
        std::vector<std::string> _list_of_options;

        double calculate_expected_error(
            const std::map<std::string, double> &ps_by_option,
            const std::map<std::string, double> &el_by_option) const;

        void translate_to_static_decisions(SystemState* system_state_tracker) override;

    };

}

#endif
