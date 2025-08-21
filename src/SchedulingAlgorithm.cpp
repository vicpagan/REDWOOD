#include "SchedulingAlgorithm.h"

#include <iostream>

#include "ProbabilityComputation.h"


/**
* @brief Instantiate a scheduling algorithm given its type/name
* @param type: the algorithm's type/name
*/
std::shared_ptr<SchedulingAlgorithm> SchedulingAlgorithm::create_scheduling_algorithm(const std::string& type, double e_fail, double delta_t, double delta_t_precision) {
    if (type == "dynamic") {
        return std::make_shared<SchedulingAlgorithmDynamic>(e_fail, delta_t, delta_t_precision);
    }
    else {
        throw std::invalid_argument("Unknown scheduling algorithm '" + type + "'");
    }
}

/**
 * @brief Calculates expected error recursively for one execution option for a single task
 *
 * @param dp Dynamic programming array
 * @param exec_option_error This is our e(x, y) for this execution option
 * @param probability_failures This is the list of our p_us
 * @param probability_success This is our e^(-lambda * m_j * delta)
 * @param m_j This is m_j in the paper
 * @param n This is n in the paper
 * @param R this is R in the paper
 * @return The expected error for the selected execution option
 */
double SchedulingAlgorithm::calculate_expected_error(std::vector<double> &dp,
                                                     const double exec_option_error,
                                                     const double probability_success,
                                                     const std::vector<double> &probability_failures,
                                                     const long m_j,
                                                     const long n,
                                                     const long R) const {

    /* BASE CASE: If theres ever less than m_j time steps remaining, we fail */
    for (long i = 0; i < m_j && i <= n; i++) {
        dp[i] = _e_fail;
    }

    for (long k = m_j; k <= n; k++) {
        double expected_error = probability_success * exec_option_error;
        for (long u = 0; u < m_j; u++) {
            expected_error += probability_failures[u] * dp[std::max((k - u - R - 1), 0L)];
        }
        dp[k] = expected_error;
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
 * @param restart_overhead Restart overhead of the host
 * @param io_read_bandwidth Bandwidth for reading input data in Bytes/sec
 * @param io_write_bandwidth Bandwidth for writing output data in Bytes/sec
 * @return The name of the best execution option
 */
std::string SchedulingAlgorithmDynamic::select_execution_option(
    ProbabilityComputation* probability_computation,
    const std::map<std::string, std::map<std::string, std::function<double(double, double)>>>& exec_options,
    const double input_data_size,
    const double input_error_level,
    const double remaining_time,
    const double restart_overhead,
    const double io_read_bandwidth,
    const double io_write_bandwidth) {

    /* Initialie min_error_level to +inf */
    double min_error_level = std::numeric_limits<double>::max();
    std::string min_execution_option;

    for (const auto& [option_name, option_functions] : exec_options) {

        /* Grab all the necessary info about the execution option */
        const auto exec_option_name = option_name;
        const auto exec_option_time = option_functions.at("t_function")(input_data_size, input_error_level);
        const auto exec_option_data = option_functions.at("d_function")(input_data_size, input_error_level);
        const auto exec_option_error = option_functions.at("e_function")(input_data_size, input_error_level);
        // std::cerr << "LOOKING AT OPTION_NAME = " << exec_option_name << std::endl;

        /* Select a delta either through calculation or by a set default */
        if (_delta_t < 0) {
            const double deltat_computation = probability_computation->compute_best_deltat(
                exec_option_time, remaining_time, _delta_t_precision);
            probability_computation->set_delta_t(deltat_computation);
        } else {
            probability_computation->set_delta_t(_delta_t);
        }

        /* Grab lambda and delta_t */
        const double selected_delta_t = probability_computation->get_delta_t();
        const double lambda = probability_computation->get_lambda();

        /* Calculate m_j, n, and R */
        const auto m_j = static_cast<long>(std::ceil(((input_data_size / io_read_bandwidth) + exec_option_time + (exec_option_data / io_write_bandwidth)) / selected_delta_t));
        const auto n = static_cast<long>(std::ceil(remaining_time / selected_delta_t));
        const auto R = static_cast<long>(std::ceil(restart_overhead / selected_delta_t));

        /* Precaculate probability of success and the list of failure probabilities for each value of u */
        const auto probability_success = exp(-lambda * m_j * selected_delta_t);
        auto probability_failures(std::vector<double>(m_j, 0.0));
        for (long u = 0; u < m_j; u++) {
            probability_failures[u] = exp(-lambda * static_cast<double>(u) * selected_delta_t) - exp(-lambda * static_cast<double>((u+1)) * selected_delta_t);
        }

        /* Calculate the expected error for the current exec option */
        auto dp(std::vector<double>(n + 1, 0.0));
        const auto expected_error_option = calculate_expected_error(dp, exec_option_error, probability_success,
            probability_failures, m_j, n, R);

        /* Take the minimum expected error of all the execution options */
        if (expected_error_option < min_error_level) {
            min_error_level = expected_error_option;
            min_execution_option = exec_option_name;
        }
    }

    return min_execution_option;
}
