#ifndef SCHEDULINGALGORITHMRANDOM_H
#define SCHEDULINGALGORITHMRANDOM_H

#include "SchedulingAlgorithm.h"
#include <random>

namespace wrench {
    class SchedulingAlgorithmRandom : public SchedulingAlgorithm {
    public:
        SchedulingAlgorithmRandom(
            const std::shared_ptr<ApplicationSpecs>& application_specs,
            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
            ProbabilityComputation* probability_computation);

        double get_optimal_expected_error() const override;

        void preprocess_decisions(double input_data_size,
                                 double input_error_level,
                                 double remaining_time,
                                 bool lower_bound) override;

        std::vector<SchedulingDecision> make_decisions(
            SystemState* system_state_tracker,
            const std::string& task_to_schedule,
            double remaining_time,
            bool minimize) override;

    private:
        std::default_random_engine _rng;
    };
}

#endif //SCHEDULINGALGORITHMRANDOM_H