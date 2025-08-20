#ifndef SCHEDULINGALGORITHM_H
#define SCHEDULINGALGORITHM_H

#include <map>
#include "ProbabilityComputation.h"

class SchedulingAlgorithm {
public:
    SchedulingAlgorithm() = default;

    virtual std::string select_execution_option(
        ProbabilityComputation* probability_computation,
        const std::map<std::string, std::map<std::string, std::function<double(double, double)>>>& exec_options,
        double input_data_size,
        double input_error_level,
        double remaining_time,
        double e_fail) = 0;

    static std::shared_ptr<SchedulingAlgorithm> create_scheduling_algorithm(const std::string& type);

protected:
    double calculate_expected_error(double exec_option_error,
                                    double probability_midpoint,
                                    double probability_success,
                                    long m_j,
                                    long n,
                                    double input_data_size,
                                    double input_error_level,
                                    double e_fail);
};

class SchedulingAlgorithmDynamic : public SchedulingAlgorithm {
    std::string select_execution_option(
        ProbabilityComputation* probability_computation,
        const std::map<std::string, std::map<std::string, std::function<double(double, double)>>>& exec_options,
        double input_data_size,
        double input_error_level,
        double remaining_time,
        double e_fail) override;
};

#endif //SCHEDULINGALGORITHM_H
