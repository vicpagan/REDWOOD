#ifndef SCHEDULINGALGORITHMDYNAMIC_H
#define SCHEDULINGALGORITHMDYNAMIC_H

#include <map>
#include <string>
#include <functional>
#include <memory>

#include "Controller.h"
#include "ProbabilityComputation.h"
#include "SchedulingAlgorithm.h"

namespace wrench {
    class SchedulingAlgorithmDynamic : public SchedulingAlgorithm {
    public:
        explicit SchedulingAlgorithmDynamic(const std::shared_ptr<ApplicationSpecs> &application_specs,
            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
            ProbabilityComputation *probability_computation) : SchedulingAlgorithm(application_specs,
                "dynamic",
                exec_options,
                probability_computation) {
        };

        std::vector<SchedulingDecision> make_decisions(
            SystemState *system_state_tracker,
            const std::string &task_to_schedule,
            double remaining_time,
            bool minimize) override;

        double get_optimal_expected_error() const override;

        void preprocess_decisions(double initial_data_size,
            double initial_error_level,
            double deadline,
            bool lower_bound) override;

    private:

        double calculate_expected_error(
            int remaining_tasks,
            int task_index,
            double running_input_data_size,
            double running_input_error_level,
            double selected_delta_t,
            std::map<const ApplicationSpecs::ExecOptionDecisionNode*, std::vector<std::pair<std::string, double>>> &dp,
            const ApplicationSpecs::ExecOptionDecisionNode* current_task_node,
            long n,
            long R,
            long deadline,
            bool lower_bound) const;

        double compute_best_delta_t(
            double initial_data_size,
            double initial_error_level,
            double deadline,
            double precision);

        void fill_preprocessing_table(
            double input_data_size,
            double input_error_level,
            double remaining_time,
            bool lower_bound);
    };
}

#endif //SCHEDULINGALGORITHMDYNAMIC_H
