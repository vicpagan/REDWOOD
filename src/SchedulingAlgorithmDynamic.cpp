#include <iostream>
#include <limits>
#include <cmath>

#include "SchedulingAlgorithmDynamic.h"
#include "ProbabilityComputation.h"
#include "JobTracker.h"

namespace wrench {
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

    std::string SchedulingAlgorithmDynamic::pick_execution_option(
        ProbabilityComputation* probability_computation,
        const std::map<std::string, std::map<std::string, std::function<double(double, double)>>>& exec_options,
        double input_data_size,
        double input_error_level,
        double remaining_time,
        double restart_overhead,
        double io_read_bandwidth,
        double io_write_bandwidth) {
        /* Initialize min_error_level to +inf */
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
            }
            else {
                probability_computation->set_delta_t(_delta_t);
            }

            /* Grab lambda and delta_t */
            const double selected_delta_t = probability_computation->get_delta_t();
            const double lambda = probability_computation->get_lambda();

            /* Calculate m_j, n, and R */
            const auto m_j = static_cast<long>(std::ceil(
                ((input_data_size / io_read_bandwidth) + exec_option_time + (exec_option_data / io_write_bandwidth)) /
                selected_delta_t));
            const auto n = static_cast<long>(std::ceil(remaining_time / selected_delta_t));
            const auto R = static_cast<long>(std::ceil(restart_overhead / selected_delta_t));

            /* Precalculate probability of success and the list of failure probabilities for each possible failure point in execution */
            const auto probability_success = exp(-lambda * static_cast<double>(m_j) * selected_delta_t);
            auto probability_failures(std::vector<double>(m_j, 0.0));
            for (long u = 0; u < m_j; u++) {
                probability_failures[u] = exp(-lambda * static_cast<double>(u) * selected_delta_t) - exp(
                    -lambda * static_cast<double>((u + 1)) * selected_delta_t);
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

        std::cerr << "DYNAMIC DECISION: " << min_execution_option << std::endl;
        return min_execution_option;
    }

    std::vector<SchedulingAlgorithm::SchedulingDecision> SchedulingAlgorithmDynamic::make_decisions(
        JobTracker* job_tracker,
        ProbabilityComputation* probability_computation,
        const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
        const string& task_to_schedule,
        double input_data_size,
        double input_error_level,
        double remaining_time,
        double restart_overhead,
        double io_read_bandwidth,
        double io_write_bandwidth) {
        std::vector<SchedulingDecision> decisions;

        // Make a decision for each host that's currently idle
        // All these decisions are independent so that makes it easy
        for (const auto& [hostname, job] : *job_tracker) {
            if (job) continue; // Host is not idle

            auto execution_option = this->pick_execution_option(
                probability_computation,
                exec_options.at(task_to_schedule),
                input_data_size,
                input_error_level,
                remaining_time,
                restart_overhead,
                io_read_bandwidth,
                io_write_bandwidth);

            decisions.push_back({hostname, task_to_schedule, execution_option});
        }
        return decisions;
    }
}
