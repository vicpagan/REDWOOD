#include "../../include/scheduling/OptionComparatorFunction.h"

ExpectedErrorComparator::ExpectedErrorComparator(const double io_read_bandwidth,
                                                 const double io_write_bandwidth,
                                                 const double e_fail) : _io_read_bandwidth(io_read_bandwidth),
                                                                        _io_write_bandwidth(io_write_bandwidth),
                                                                        _e_fail(e_fail) {
}

double ExpectedErrorComparator::comp_value(ProbabilityComputation *probability_computation,
                                           const std::map<std::string, std::function<double(double, double)> > &option_functions,
                                           const double input_data_size,
                                           const double input_error_level,
                                           const double remaining_time) const {

    const double exec_option_time = option_functions.at("t_function")(input_data_size, input_error_level);
    const double exec_option_data = option_functions.at("d_function")(input_data_size, input_error_level);
    const double exec_option_error = option_functions.at("e_function")(input_data_size, input_error_level);

    const double exec_option_time_total =
            (input_data_size / _io_read_bandwidth) + exec_option_time +
            (exec_option_data / _io_write_bandwidth);

    const double exec_option_probability_success =
            probability_computation->compute_probability(exec_option_time_total, remaining_time, false);

    return exec_option_probability_success * exec_option_error +
           (1.0 - exec_option_probability_success) * _e_fail;
}

ProbabilitySuccessComparator::ProbabilitySuccessComparator(const double io_read_bandwidth,
                                                           const double io_write_bandwidth) : _io_read_bandwidth(io_read_bandwidth),
                                                                                              _io_write_bandwidth(io_write_bandwidth) {
}

double ProbabilitySuccessComparator::comp_value(ProbabilityComputation *probability_computation,
                                                const std::map<std::string, std::function<double(double, double)> > &
                                                option_functions,
                                                const double input_data_size,
                                                const double input_error_level,
                                                const double remaining_time) const {

    const double exec_option_time = option_functions.at("t_function")(input_data_size, input_error_level);
    const double exec_option_data = option_functions.at("d_function")(input_data_size, input_error_level);
    const double exec_option_time_total = (input_data_size / _io_read_bandwidth) + exec_option_time +
                                          (exec_option_data / _io_write_bandwidth);

    return probability_computation->compute_probability(exec_option_time_total, remaining_time, false);
}

ErrorLevelComparator::ErrorLevelComparator(const double io_read_bandwidth,
                                           const double io_write_bandwidth,
                                           const double prob_success_threshold) : _io_read_bandwidth(io_read_bandwidth),
                                                                                  _io_write_bandwidth(io_write_bandwidth),
                                                                                  _prob_success_threshold(prob_success_threshold) {
}

double ErrorLevelComparator::comp_value(ProbabilityComputation *probability_computation,
                                        const std::map<std::string, std::function<double(double, double)> > &option_functions,
                                        const double input_data_size,
                                        const double input_error_level,
                                        const double remaining_time) const {

    const double exec_option_time = option_functions.at("t_function")(input_data_size, input_error_level);
    const double exec_option_data = option_functions.at("d_function")(input_data_size, input_error_level);
    const double exec_option_error = option_functions.at("e_function")(input_data_size, input_error_level);
    const double exec_option_time_total = (input_data_size / _io_read_bandwidth) + exec_option_time +
                                          (exec_option_data / _io_write_bandwidth);

    const double probability_success = probability_computation->compute_probability(exec_option_time_total, remaining_time, false);

    if (probability_success < _prob_success_threshold) {
        return std::numeric_limits<double>::max();
    }
    return exec_option_error;
}

SuccessErrorRatioComparator::SuccessErrorRatioComparator(const double io_read_bandwidth,
                                                         const double io_write_bandwidth) : _io_read_bandwidth(io_read_bandwidth),
                                                                                            _io_write_bandwidth(io_write_bandwidth) {
}

double SuccessErrorRatioComparator::comp_value(ProbabilityComputation *probability_computation,
                                               const std::map<std::string, std::function<double(double, double)> > &option_functions,
                                               const double input_data_size,
                                               const double input_error_level,
                                               const double remaining_time) const {

    const double exec_option_time = option_functions.at("t_function")(input_data_size, input_error_level);
    const double exec_option_data = option_functions.at("d_function")(input_data_size, input_error_level);
    const double exec_option_error = option_functions.at("e_function")(input_data_size, input_error_level);
    const double exec_option_time_total = (input_data_size / _io_read_bandwidth) + exec_option_time +
                                          (exec_option_data / _io_write_bandwidth);

    const double probability_success = probability_computation->compute_probability(exec_option_time_total, remaining_time, false);

    return probability_success / exec_option_error;
}
