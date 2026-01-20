#ifndef SCHEDULINGALGORITHMSTATIC_H
#define SCHEDULINGALGORITHMSTATIC_H

#include <map>
#include <string>
#include <functional>
#include <memory>

#include "Controller.h"
#include "ProbabilityComputation.h"
#include "SchedulingAlgorithm.h"

namespace wrench {
    class SchedulingAlgorithmStatic : public SchedulingAlgorithm {
    public:
        explicit SchedulingAlgorithmStatic(const std::shared_ptr<ApplicationSpecs> &application_specs,
            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
            ProbabilityComputation *probability_computation,
            OptionComparatorFunction* comparator_function) : SchedulingAlgorithm(application_specs,
                "static",
                exec_options,
                probability_computation,
                comparator_function) {
        };

        double get_optimal_expected_error() const override { return 0.0; }

        void preprocess_decisions(double initial_data_size,
            double initial_error_level,
            double deadline,
            bool lower_bound) override;

        std::vector<SchedulingDecision> make_decisions(
            SystemState *system_state_tracker,
            const std::string &task_to_schedule,
            double remaining_time,
            bool minimize) override;

    private:
        void fill_preprocessing_table(
            double input_data_size,
            double input_error_level,
            double remaining_time,
            bool lower_bound);
    };
}

#endif //SCHEDULINGALGORITHMSTATIC_H
