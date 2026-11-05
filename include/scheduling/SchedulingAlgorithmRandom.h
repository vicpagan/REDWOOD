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
            Discretization* discretization);

        void preprocess_host_decisions(const std::string& hostname,
                                 double initial_data_size,
                                 double initial_error_level,
                                 double deadline,
                                 bool lower_bound) override;

        std::vector<SchedulingDecision> make_decisions(
            SystemState* system_state_tracker,
            double remaining_time) override;

        /**
         * @brief Does nothing (no decisions are stored)
         * @param hostname The host
         */
        void reset_host_preprocessed_decisions(const std::string& hostname) override {
            // no preprocessing, so nothing to reset
        }

        /**
         * @brief Does nothing (no decisions are stored)
         */
        void reset_all_preprocessed_decisions() override {
            // no preprocessing, so nothing to reset
        }

    private:
        std::default_random_engine _rng;
    };
}

#endif //SCHEDULINGALGORITHMRANDOM_H