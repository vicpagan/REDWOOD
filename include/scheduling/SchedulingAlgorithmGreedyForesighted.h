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
            const std::string& name,
            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
            ProbabilityComputation* probability_computation,
            OptionComparatorFunction* comparator_function)
            : SchedulingAlgorithmGreedy(application_specs, name, exec_options,
                                       probability_computation, comparator_function) {
        }

        void preprocess_decisions(double initial_data_size,
                                 double initial_error_level,
                                 double deadline,
                                 bool lower_bound) override = 0;

        std::vector<SchedulingDecision> make_decisions(
            SystemState* system_state_tracker,
            const std::string& task_to_schedule,
            double input_data_size,
            double input_error_level,
            double remaining_time) override;

        void reset_preprocessed_decisions() override {
            SchedulingAlgorithmGreedy::reset_preprocessed_decisions();
            _all_combinations.clear();
            _nodes_per_combo_decision.clear();
        }

    protected:
        std::vector<std::vector<std::string>> _all_combinations;
        std::map<std::vector<std::string>, long> _nodes_per_combo_decision;

        void collect_combinations(const ApplicationSpecs::ExecOptionDecisionNode *node,
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
            long n, long R,
            long deadline,
            bool lower_bound) const;

        double calculate_error_level_one_host(
            const ApplicationSpecs::ExecOptionDecisionNode* current_node,
            const std::vector<std::string> &combo,
            int task_index) const;

        double calculate_expected_error(
            const std::map<std::vector<std::string>, double> &ps_by_combo,
            const std::map<std::vector<std::string>, double> &el_by_combo) const;

    };

}

#endif
