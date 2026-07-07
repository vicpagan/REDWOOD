#ifndef SCHEDULINGALGORITHMSTATICFORESIGHTED_H
#define SCHEDULINGALGORITHMSTATICFORESIGHTED_H

#include "SchedulingAlgorithmStatic.h"

namespace wrench {
    /**
     * Static Foresight (Online Static):
     * Uses dynamic programming to consider future tasks when making decisions.
     * Preprocesses decisions by running DP for the entire task chain,
     * then extracts the first optimal decision for each task.
     */
    class SchedulingAlgorithmStaticForesighted : public SchedulingAlgorithmStatic {
    public:
        SchedulingAlgorithmStaticForesighted(
            const std::shared_ptr<ApplicationSpecs>& application_specs,
            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
            ProbabilityComputation* probability_computation,
            OptionComparatorFunction* comparator_function)
            : SchedulingAlgorithmStatic(application_specs, "static_foresighted", exec_options,
                                       probability_computation, comparator_function) {}

        void preprocess_host_decisions(const std::string& hostname,
                                 double initial_data_size,
                                 double initial_error_level,
                                 double deadline,
                                 bool lower_bound) override;

        void reset_host_preprocessed_decisions(const std::string &hostname) override {
            SchedulingAlgorithmStatic::reset_host_preprocessed_decisions(hostname);
        }

        void reset_all_preprocessed_decisions() override {
            SchedulingAlgorithmStatic::reset_all_preprocessed_decisions();
        }

    private:

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
            int task_index) const;

    };

}

#endif
