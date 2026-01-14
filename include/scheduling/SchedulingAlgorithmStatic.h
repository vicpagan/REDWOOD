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
        explicit SchedulingAlgorithmStatic(const std::shared_ptr<ApplicationSpecs> &application_specs) : SchedulingAlgorithm(
            application_specs, "static") {
        };

        double get_optimal_expected_error() const override { return 0.0; }

        void preprocess_decisions(ProbabilityComputation* probability_computation,
                                  const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
                                  double input_data_size,
                                  double input_error_level,
                                  double remaining_time,
                                  bool lower_bound) override {}

        std::vector<SchedulingDecision> make_decisions(
            SystemState *system_state_tracker,
            ProbabilityComputation *probability_computation,
            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double
                (double, double)> > > > &exec_options,
            const std::string &task_to_schedule,
            double input_data_size,
            double input_error_level,
            double remaining_time,
            OptionComparatorFunction *comparator_function,
            bool minimize) override;

    private:
        std::string pick_execution_option(
            ProbabilityComputation *probability_computation,
            const std::map<std::string, std::map<std::string, std::function<double(double, double)> > > &exec_options,
            double input_data_size,
            double input_error_level,
            double remaining_time,
            OptionComparatorFunction *comparator_function,
            bool minimize) const;
    };
}

#endif //SCHEDULINGALGORITHMSTATIC_H
