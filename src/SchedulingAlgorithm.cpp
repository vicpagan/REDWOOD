#include "SchedulingAlgorithm.h"
#include "ProbabilityComputation.h"


/**
* @brief Instantiate a scheduling algorithm given its type/name
* @param type: the algorithm's type/name
*/
std::shared_ptr<SchedulingAlgorithm> SchedulingAlgorithm::create_scheduling_algorithm(const std::string &type) {
  if (type == "dynamic") {
    return std::make_shared<SchedulingAlgorithmDynamic>();
  } else {
      throw std::invalid_argument("Unknown scheduling algorithm '" + type + "'");
  }
}

/**
     * @brief Calculates expected error recursively for one execution option for a single task
     *
     * @param exec_option_error This is our e(x, y,) for this execution option
     * @param probability_midpoint This is our p_u
     * @param probability_success This is our e^(-lambda * m_j * delta)
     * @param m_j This is m_j in the paper
     * @param n THis is n in the paper
     * @param input_data_size This is our x
     * @param input_error_level This is our y
     * @return The expected error for the selected execution option
     */
double SchedulingAlgorithm::calculate_expected_error(double exec_option_error,
                                            double probability_midpoint,
                                            double probability_success,
                                            long m_j,
                                            long n,
                                            double input_data_size,
                                            double input_error_level,
                                            double e_fail) {
    if (n < m_j) {
        return e_fail;
    }

    double reward_success = probability_success * exec_option_error;
    double fail_punishment = 0.0;
    for (long i = 0; i < m_j; i++) {
        fail_punishment += (probability_midpoint * calculate_expected_error(
            exec_option_error, probability_midpoint, probability_success, m_j, n - i - 1, input_data_size,
            input_error_level, e_fail));
    }
    return reward_success + fail_punishment;
}


/**
 * DYNAMIC SCHEDULING ALGORITHM
 *
 * @brief Selects the best execution option based on the lowest E(x, y, n)
 *
 * @param exec_options Map of execution options for the current task
 * @param input_data_size This is our x
 * @param input_error_level This is our y
 * @param remaining_time This is our n, which is the remaining time until the deadline
 * @param e_fail The error level if failure
 * @return The name of the best execution option
 */
std::string SchedulingAlgorithmDynamic::select_execution_option(
    ProbabilityComputation *probability_computation,
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
        const auto exec_option_error = option_functions.at("e_function")(input_data_size, input_error_level);

        double deltat_computation = probability_computation->compute_best_deltat(
            exec_option_time, remaining_time, 1e-3);
        probability_computation->set_delta_t(deltat_computation);
        double probability_midpoint = probability_computation->compute_probability_midpoint(
            exec_option_time, remaining_time);

        // TODO: m_j does not take I/O into account just yet. Need to set up bandwidth.
        // TODO: HENRI: The Controller now has a _io_read_bandwidth and _io_write_bandwidth variables
        // TODO         that store the I/O bandwidths in byte/sec
        auto m_j = static_cast<long>(std::ceil(exec_option_time / deltat_computation));
        auto n = static_cast<long>(std::ceil(remaining_time / deltat_computation));
        auto probability_success = exp(-probability_computation->get_lambda() * m_j * deltat_computation);

        auto expected_error_option = calculate_expected_error(exec_option_error, probability_midpoint,
                                                              probability_success, m_j, n, input_data_size,
                                                              input_error_level, e_fail);
        if (expected_error_option < min_error_level) {
            min_error_level = expected_error_option;
            min_execution_option = exec_option_name;
        }
    }

    return min_execution_option;
}