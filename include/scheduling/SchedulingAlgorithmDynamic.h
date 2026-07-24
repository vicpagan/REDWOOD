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
        explicit SchedulingAlgorithmDynamic(
            const std::shared_ptr<ApplicationSpecs> &application_specs,
            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
            ProbabilityComputation *probability_computation) : SchedulingAlgorithm(application_specs,
                "dynamic",
                exec_options,
                probability_computation) {
        };

        std::vector<SchedulingDecision> make_decisions(
            SystemState *system_state_tracker,
            double remaining_time) override;

        double get_expected_error() const { return _optimal_EV; };

        void preprocess_host_decisions(
            const std::string& hostname,
            double initial_data_size,
            double initial_error_level,
            double deadline,
            bool lower_bound) override;

        void reset_all_preprocessed_decisions() override {
            _preprocessed_decisions_by_host.clear();
        }

        void reset_host_preprocessed_decisions(const std::string& hostname) override {
            _preprocessed_decisions_by_host.erase(hostname);
        }

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

        // FIXME: This needs to be moved to another file
        // double compute_best_delta_t(
        //     double initial_data_size,
        //     double initial_error_level,
        //     double deadline,
        //     double precision);

        void fill_host_preprocessing_table(
            const std::string& hostname,
            double input_data_size,
            double input_error_level,
            double remaining_time,
            bool lower_bound);


        double _optimal_EV = 0;
        std::map<std::string, std::map<const ApplicationSpecs::ExecOptionDecisionNode*, std::vector<std::pair<long, std::string>>>> _preprocessed_decisions_by_host;
    };
}

#endif //SCHEDULINGALGORITHMDYNAMIC_H
