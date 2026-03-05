#ifndef SCHEDULINGALGORITHMSTATIC_H
#define SCHEDULINGALGORITHMSTATIC_H

#include "SchedulingAlgorithm.h"

namespace wrench {
    class SchedulingAlgorithmStatic : public SchedulingAlgorithm {
    public:
        SchedulingAlgorithmStatic(
            const std::shared_ptr<ApplicationSpecs>& application_specs,
            const std::string& name,
            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
            ProbabilityComputation* probability_computation,
            OptionComparatorFunction* comparator_function)
            : SchedulingAlgorithm(application_specs, name, exec_options, probability_computation),
              _comparator_function(comparator_function) {}

        double get_expected_error() const override { return _expected_error; }

        void preprocess_decisions(double input_data_size,
            double input_error_level,
            double remaining_time,
            bool lower_bound) override = 0;

        std::vector<SchedulingDecision> make_decisions(
            SystemState* system_state_tracker,
            const std::string& task_to_schedule,
            double input_data_size,
            double input_error_level,
            double remaining_time) override;

        void reset_preprocessed_decisions() override {
            _static_decisions.clear();
        }

    protected:
        OptionComparatorFunction* _comparator_function;
        std::map<std::string, std::string> _static_decisions;
        double _expected_error = 0.0;
    };
}

#endif //SCHEDULINGALGORITHMSTATIC_H