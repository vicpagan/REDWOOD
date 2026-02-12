#ifndef SCHEDULINGALGORITHMSTATIC_H
#define SCHEDULINGALGORITHMSTATIC_H

#include "SchedulingAlgorithm.h"

namespace wrench {
    class SchedulingAlgorithmStatic : public SchedulingAlgorithm {
    public:
        SchedulingAlgorithmStatic(
            const std::shared_ptr<ApplicationSpecs>& application_specs,
            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
            ProbabilityComputation* probability_computation,
            OptionComparatorFunction* comparator_function)
            : SchedulingAlgorithm(application_specs, "static", exec_options, probability_computation) {

            _comparator_function = comparator_function;
        }

        double get_optimal_expected_error() const override;

        void preprocess_decisions(double input_data_size,
                                 double input_error_level,
                                 double deadline,
                                 bool minimize) override;

        std::vector<SchedulingDecision> make_decisions(
            SystemState* system_state_tracker,
            const std::string& task_to_schedule,
            double remaining_time,
            bool minimize) override;

    private:

        OptionComparatorFunction* _comparator_function;

        std::map<std::string, std::string> _static_decisions;
        double _expected_error = 0.0;
    };
}

#endif //SCHEDULINGALGORITHMSTATIC_H