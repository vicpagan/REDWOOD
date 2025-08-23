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
        SchedulingAlgorithmDynamic(const double e_fail, const double delta_t,
                                   const double delta_t_precision) : SchedulingAlgorithm(
            e_fail, delta_t, delta_t_precision, "dynamic") {
        };

        std::vector<SchedulingDecision> make_decisions(
            JobTracker* job_tracker,
            ProbabilityComputation* probability_computation,
            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
            const std::string& task_to_schedule,
            double input_data_size,
            double input_error_level,
            double remaining_time,
            double restart_overhead,
            double io_read_bandwidth,
            double io_write_bandwidth) override;

    private:
        std::string pick_execution_option(
            ProbabilityComputation* probability_computation,
            const std::map<std::string, std::map<std::string, std::function<double(double, double)>>>& exec_options,
            double input_data_size,
            double input_error_level,
            double remaining_time,
            double restart_overhead,
            double io_read_bandwidth,
            double io_write_bandwidth);

    };
}

#endif //SCHEDULINGALGORITHMDYNAMIC_H
