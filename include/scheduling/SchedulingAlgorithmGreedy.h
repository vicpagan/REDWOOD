#ifndef SCHEDULINGALGORITHMGREEDY_H
#define SCHEDULINGALGORITHMGREEDY_H

#include "SchedulingAlgorithm.h"

namespace wrench {
    class SchedulingAlgorithmGreedy : public SchedulingAlgorithm {
    public:
        SchedulingAlgorithmGreedy(
            const std::shared_ptr<ApplicationSpecs>& application_specs,
            const std::string& name,
            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
            ProbabilityComputation* probability_computation,
            OptionComparatorFunction* comparator_function)
            : SchedulingAlgorithm(application_specs, name, exec_options, probability_computation),
              _comparator_function(comparator_function),
              _num_compute_nodes(application_specs->get_num_compute_nodes()) {
        }

        double get_expected_error() const override { return _expected_error; }

        void preprocess_host_decisions(const std::string& hostname,
            double initial_data_size,
            double initial_error_level,
            double remaining_time,
            bool lower_bound) override = 0;

        virtual void initial_decisions(const std::string& hostname,
                                 double initial_data_size,
                                 double initial_error_level,
                                 double deadline,
                                 bool lower_bound) = 0;

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

        virtual void translate_to_static_decisions(SystemState* system_state_tracker) = 0;

        std::map<std::string, std::map<std::string, std::string>> _static_decisions_per_host;

        OptionComparatorFunction* _comparator_function;
        long _num_compute_nodes;
        double _expected_error = 0.0;
    };
}

#endif //SCHEDULINGALGORITHMGREEDY_H