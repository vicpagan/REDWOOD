#ifndef SCHEDULINGALGORITHM_H
#define SCHEDULINGALGORITHM_H

#include <map>
#include "ProbabilityComputation.h"

class SchedulingAlgorithm {
public:
    virtual ~SchedulingAlgorithm() = default;
    SchedulingAlgorithm(const double e_fail, const double delta_t, const double delta_t_precision) : _e_fail(e_fail), _delta_t(delta_t), _delta_t_precision(delta_t_precision) {};

    virtual std::string select_execution_option(
        ProbabilityComputation* probability_computation,
        const std::map<std::string, std::map<std::string, std::function<double(double, double)>>>& exec_options,
        double input_data_size,
        double input_error_level,
        double remaining_time,
        double io_read_bandwidth,
        double io_write_bandwidth) = 0;

    static std::shared_ptr<SchedulingAlgorithm> create_scheduling_algorithm(const std::string& type, double e_fail, double delta_t, double delta_t_precision);

protected:
    double calculate_expected_error(std::vector<double> &dp,
                                    double exec_option_error,
                                    double probability_success,
                                    std::vector<double> &probability_failures,
                                    long m_j,
                                    long n);

    double _e_fail;
    double _delta_t;
    double _delta_t_precision;
};

class SchedulingAlgorithmDynamic : public SchedulingAlgorithm {

public:
    SchedulingAlgorithmDynamic(const double e_fail, const double delta_t, const double delta_t_precision) : SchedulingAlgorithm(e_fail, delta_t, delta_t_precision) {};

    std::string select_execution_option(
        ProbabilityComputation* probability_computation,
        const std::map<std::string, std::map<std::string, std::function<double(double, double)>>>& exec_options,
        double input_data_size,
        double input_error_level,
        double remaining_time,
        double io_read_bandwidth,
        double io_write_bandwidth) override;
};

#endif //SCHEDULINGALGORITHM_H
