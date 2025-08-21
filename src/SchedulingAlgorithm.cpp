#include "SchedulingAlgorithm.h"

#include <iostream>

#include "ProbabilityComputation.h"


/**
* @brief Instantiate a scheduling algorithm given its type/name
* @param type: the algorithm's type/name
*/
std::shared_ptr<SchedulingAlgorithm> SchedulingAlgorithm::create_scheduling_algorithm(const std::string& type, double io_read_bandwidth, double io_write_bandwidht, double delta_t, double delta_t_precision) {
    if (type == "dynamic") {
        return std::make_shared<SchedulingAlgorithmDynamic>(io_read_bandwidth, io_write_bandwidht, delta_t, delta_t_precision);
    }
    else {
        throw std::invalid_argument("Unknown scheduling algorithm '" + type + "'");
    }
}

/**
 * @brief Calculates expected error recursively for one execution option for a single task
 *
 * @param dp Dynamic programming array
 * @param exec_option_error This is our e(x, y,) for this execution option
 * @param probability_failures This is our p_u
 * @param probability_success This is our e^(-lambda * m_j * delta)
 * @param m_j This is m_j in the paper
 * @param n THis is n in the paper
 * @param input_data_size This is our x
 * @param input_error_level This is our y
 * @param e_fail Failure penalty
 * @return The expected error for the selected execution option
 */
double SchedulingAlgorithm::calculate_expected_error(std::vector<double> &dp,
                                                     double exec_option_error,
                                                     double probability_success,
                                                     std::vector<double> &probability_failures,
                                                     long m_j,
                                                     long n,
                                                     double e_fail) {

    // Base cases: if less than m_j steps, we always fail
    for (long i = 0; i < m_j && i <= n; i++) {
        dp[i] = e_fail;
    }

    // Fill dp bottom-up
    for (long k = m_j; k <= n; k++) {
        double reward_success = probability_success * exec_option_error;
        double fail_punishment = 0.0;

        for (long u = 0; u < m_j; u++) {
            reward_success += probability_failures[u] * dp[k - u - 1];
        }

        dp[k] = reward_success + fail_punishment;
    }

    return dp[n];
}


/**
 * DYNAMIC SCHEDULING ALGORITHM
 *
 * @brief Selects the best execution option based on the lowest E(x, y, n)
 * @param probability_computation The probability computation utility
 * @param exec_options Map of execution options for the current task
 * @param input_data_size This is our x
 * @param input_error_level This is our y
 * @param remaining_time This is our n, which is the remaining time until the deadline
 * @param e_fail The error level if failure
 * @return The name of the best execution option
 */
std::string SchedulingAlgorithmDynamic::select_execution_option(
    ProbabilityComputation* probability_computation,
    const std::map<std::string, std::map<std::string, std::function<double(double, double)>>>& exec_options,
    const double input_data_size,
    const double input_error_level,
    const double remaining_time,
    const double e_fail) {

    double min_error_level = std::numeric_limits<double>::max();
    std::string min_execution_option;

    for (const auto& [option_name, option_functions] : exec_options) {
        const auto exec_option_name = option_name;
        const auto exec_option_time = option_functions.at("t_function")(input_data_size, input_error_level);
        const auto exec_option_data = option_functions.at("d_function")(input_data_size, input_error_level);
        const auto exec_option_error = option_functions.at("e_function")(input_data_size, input_error_level);
        // std::cerr << "LOOKING AT OPTION_NAME = " << exec_option_name << std::endl;

        if (_delta_t < 0) {
            double deltat_computation = probability_computation->compute_best_deltat(
                exec_option_time, remaining_time, _delta_t_precision);
            probability_computation->set_delta_t(deltat_computation);
        } else {
            probability_computation->set_delta_t(_delta_t);
        }

        double selected_delta_t = probability_computation->get_delta_t();
        double lambda = probability_computation->get_lambda();

        auto m_j = static_cast<long>(std::ceil(((input_data_size / _io_read_bandwidth) + exec_option_time + (exec_option_data / _io_write_bandwidth)) / selected_delta_t));
        auto n = static_cast<long>(std::ceil(remaining_time / selected_delta_t));

        auto probability_success = exp(-lambda * m_j * selected_delta_t);
        auto probability_failures(std::vector<double>(m_j, 0.0));
        for (long u = 0; u < m_j; u++) {
            probability_failures[u] = exp(-lambda * static_cast<double>(u) * selected_delta_t) - exp(-lambda * static_cast<double>((u+1)) * selected_delta_t);
        }

        auto dp(std::vector<double>(n + 1, 0.0));
        auto expected_error_option = calculate_expected_error(dp, exec_option_error, probability_success,
            probability_failures, m_j, n, e_fail);

        if (expected_error_option < min_error_level) {
            min_error_level = expected_error_option;
            min_execution_option = exec_option_name;
        }
    }

    return min_execution_option;
}
