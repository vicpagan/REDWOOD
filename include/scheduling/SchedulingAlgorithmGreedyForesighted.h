#ifndef SCHEDULINGALGORITHMGREEDYFORESIGHTED_H
#define SCHEDULINGALGORITHMGREEDYFORESIGHTED_H

#include "SchedulingAlgorithmGreedy.h"

namespace wrench {
    /**
     * Greedy Foresight (Online Greedy):
     * Uses dynamic programming to consider future tasks when making decisions.
     * Preprocesses decisions by running DP for the entire task chain,
     * then extracts the first optimal decision for each task.
     */
    class SchedulingAlgorithmGreedyForesighted : public SchedulingAlgorithmGreedy {
    public:
        SchedulingAlgorithmGreedyForesighted(
            const std::shared_ptr<ApplicationSpecs>& application_specs,
            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
            ProbabilityComputation* probability_computation,
            OptionComparatorFunction* comparator_function)
            : SchedulingAlgorithmGreedy(application_specs, "greedy_foresighted", exec_options,
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

        // FIXME: Change combinations storing to be reused for every scheduling calculation
        void reset_host_preprocessed_decisions(const std::string& hostname) override {
            SchedulingAlgorithmGreedy::reset_host_preprocessed_decisions(hostname);
            _nodes_per_combo_decision.clear();
        }

        void reset_all_preprocessed_decisions() override {
            SchedulingAlgorithmGreedy::reset_all_preprocessed_decisions();
            _nodes_per_combo_decision.clear();
        }

    protected:
        std::map<std::vector<std::string>, long> _nodes_per_combo_decision;

        void collect_combinations(std::vector<std::vector<std::string>> &all_combinations,
            const ApplicationSpecs::ExecOptionDecisionNode *node,
            std::vector<std::string> &current_path);

        double calculate_prob_success_one_host(
            int remaining_tasks,
            int task_index,
            double running_input_data_size,
            double running_input_error_level,
            double selected_delta_t,
            std::map<const ApplicationSpecs::ExecOptionDecisionNode*, std::vector<double>> &dp,
            const ApplicationSpecs::ExecOptionDecisionNode* current_task_node,
            const std::vector<std::string> &combo,
            int relative_task_index,
            long n, long R,
            long deadline,
            bool lower_bound) const;

        double calculate_error_level_one_host(
            const ApplicationSpecs::ExecOptionDecisionNode* current_node,
            const std::vector<std::string> &combo,
            int relative_task_index) const;

        double calculate_expected_error(
            const std::vector<std::vector<std::string>> &all_combinations,
            const std::map<std::vector<std::string>, double> &ps_by_combo,
            const std::map<std::vector<std::string>, double> &el_by_combo) const;

        void translate_to_static_decisions(SystemState* system_state_tracker) override;

    };

}

#endif
