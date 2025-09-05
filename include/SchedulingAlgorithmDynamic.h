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
        SchedulingAlgorithmDynamic(const double e_fail,
                                   std::string delta_t_scheme,
                                   const double delta_t_parameter,
                                   const double restart_overhead,
                                   const double io_read_bandwidth,
                                   const double io_write_bandwidth) : SchedulingAlgorithm(
            e_fail, std::move(delta_t_scheme), delta_t_parameter, restart_overhead, io_read_bandwidth, io_write_bandwidth, "dynamic") {
        };

        std::vector<SchedulingDecision> make_decisions(
            JobTracker* job_tracker,
            ProbabilityComputation* probability_computation,
            const std::map<std::string, std::map<
                               std::string, std::map<std::string, std::function<double(double, double)>>>>&
            exec_options,
            const std::string& task_to_schedule,
            double input_data_size,
            double input_error_level,
            double remaining_time) override;

    private:
        std::string pick_execution_option(
            ProbabilityComputation* probability_computation,
            const std::map<std::string, std::map<std::string, std::function<double(double, double)>>>& exec_options,
            double input_data_size,
            double input_error_level,
            double remaining_time);
    };
}

#endif //SCHEDULINGALGORITHMDYNAMIC_H
