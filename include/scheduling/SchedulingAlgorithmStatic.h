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

        void preprocess_host_decisions(const std::string& hostname,
            double initial_data_size,
            double initial_error_level,
            double deadline,
            bool lower_bound) override = 0;

        std::vector<SchedulingDecision> make_decisions(
            SystemState* system_state_tracker,
            double remaining_time) override;

        void reset_host_preprocessed_decisions(const std::string& hostname) override {
            _static_decisions_per_host.erase(hostname);
        }

        void reset_all_preprocessed_decisions() override {
            _static_decisions_per_host.clear();
            _expected_error = 0.0;
        }

    protected:
        OptionComparatorFunction* _comparator_function;
        std::map<std::string, std::map<std::string, std::string>> _static_decisions_per_host;
        double _expected_error = 0.0;
    };
}

#endif //SCHEDULINGALGORITHMSTATIC_H