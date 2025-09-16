#ifndef REDWOOD_OPTIONCOMPARATORFUNCTION_H
#define REDWOOD_OPTIONCOMPARATORFUNCTION_H

#include <string>
#include <map>
#include <functional>
#include "ProbabilityComputation.h"

class OptionComparatorFunction {
public:
    virtual ~OptionComparatorFunction() = default;

    virtual double comp_value(
        ProbabilityComputation *probability_computation,
        const std::map<std::string, std::function<double(double, double)> > &option_functions,
        double input_data_size,
        double input_error_level,
        double remaining_time
    ) const = 0;
};

class ExpectedErrorComparator : public OptionComparatorFunction {
public:
    ExpectedErrorComparator(double io_read_bandwidth,
                            double io_write_bandwidth,
                            double e_fail);

    double comp_value(
        ProbabilityComputation *probability_computation,
        const std::map<std::string, std::function<double(double, double)> > &option_functions,
        double input_data_size,
        double input_error_level,
        double remaining_time) const override;

private:
    double _io_read_bandwidth;
    double _io_write_bandwidth;
    double _e_fail;
};

class ProbabilitySuccessComparator : public OptionComparatorFunction {
public:
    ProbabilitySuccessComparator(double io_read_bandwidth,
                      double io_write_bandwidth);

    double comp_value(
        ProbabilityComputation *probability_computation,
        const std::map<std::string, std::function<double(double, double)> > &option_functions,
        double input_data_size,
        double input_error_level,
        double remaining_time) const override;

private:
    double _io_read_bandwidth;
    double _io_write_bandwidth;
};

class ErrorLevelComparator : public OptionComparatorFunction {
public:
    ErrorLevelComparator(double io_read_bandwidth, double io_write_bandwidth, double prob_success_threshold);

    double comp_value(
        ProbabilityComputation *probability_computation,
        const std::map<std::string, std::function<double(double, double)> > &option_functions,
        double input_data_size,
        double input_error_level,
        double remaining_time) const override;

private:
    double _io_read_bandwidth;
    double _io_write_bandwidth;
    double _prob_success_threshold;
};

class SuccessErrorRatioComparator : public OptionComparatorFunction {
public:
    SuccessErrorRatioComparator(double io_read_bandwidth, double io_write_bandwidth);

    double comp_value(
        ProbabilityComputation *probability_computation,
        const std::map<std::string, std::function<double(double, double)> > &option_functions,
        double input_data_size,
        double input_error_level,
        double remaining_time) const override;

private:
    double _io_read_bandwidth;
    double _io_write_bandwidth;
};

#endif //REDWOOD_OPTIONCOMPARATORFUNCTION_H
