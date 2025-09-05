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
     * @return The name of the best execution option
     */

    std::string SchedulingAlgorithmDynamic::pick_execution_option(
        ProbabilityComputation* probability_computation,
        const std::map<std::string, std::map<std::string, std::function<double(double, double)>>>& exec_options,
        double input_data_size,
        double input_error_level,
        double remaining_time) {
        /* Initialize selected_delta_t to +inf */
        double selected_delta_t = std::numeric_limits<double>::max();
        std::map<std::string, std::map<std::string, double>> exec_option_metrics;
        const double lambda = probability_computation->get_lambda();
        for (const auto& [option_name, option_functions] : exec_options) {
            /* Grab all the necessary info about the execution option */
            exec_option_metrics.at(option_name).at("t_function") = option_functions.at("t_function")(input_data_size, input_error_level);
            exec_option_metrics.at(option_name).at("d_function") = option_functions.at("d_function")(input_data_size, input_error_level);
            exec_option_metrics.at(option_name).at("e_function") = option_functions.at("e_function")(input_data_size, input_error_level);
            // std::cerr << "LOOKING AT OPTION_NAME = " << exec_option_name << std::endl;

            /* Select a delta based on the scheme */
            if (_delta_t_scheme == "fixed") {
                // _delta_t_parameter is our fixed delta_t value
                selected_delta_t = _delta_t_parameter;
            } else if (_delta_t_scheme == "compute_once" && "compute_always") {
                selected_delta_t = std::min(selected_delta_t, probability_computation->compute_best_deltat(
                exec_option_metrics.at(option_name).at("t_function"), remaining_time, _delta_t_parameter));
            } else {
                throw std::invalid_argument("Unknown delta_t_scheme '" + _delta_t_scheme + "'");
            }
        }
        probability_computation->set_delta_t(selected_delta_t);

        /* Calculate m_js, n, and R */
        std::map<std::string, long> m_j;
        std::map<std::string, double> probability_success;
        std::map<std::string, std::vector<double>> probability_failures;
        for (const auto& [option_name, option_functions] : exec_options) {
            m_j.at(option_name) = static_cast<long>(std::ceil(
            ((input_data_size / _io_read_bandwidth) + exec_option_metrics.at(option_name).at("t_function") + (exec_option_metrics.at(option_name).at("d_function") / _io_write_bandwidth)) /
            selected_delta_t));

            /* Precalculate probability of success and the list of failure probabilities for each possible failure point in execution */
            probability_success.at(option_name) = exp(-lambda * static_cast<double>(m_j.at(option_name)) * selected_delta_t);
            for (long u = 0; u < m_j.at(option_name); u++) {
                probability_failures.at(option_name)[u] = exp(-lambda * static_cast<double>(u) * selected_delta_t) - exp(
                    -lambda * static_cast<double>((u + 1)) * selected_delta_t);
            }
        }
        const auto n = static_cast<long>(std::ceil(remaining_time / selected_delta_t));
        const auto R = static_cast<long>(std::ceil(_restart_overhead / selected_delta_t));

        // std::cerr << "DYNAMIC DECISION: " << min_execution_option << std::endl;
        return calculate_expected_error();
    }

    std::vector<SchedulingAlgorithm::SchedulingDecision> SchedulingAlgorithmDynamic::make_decisions(
        JobTracker* job_tracker,
        ProbabilityComputation* probability_computation,
        const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
        const string& task_to_schedule,
        double input_data_size,
        double input_error_level,
        double remaining_time) {
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
                remaining_time);

            decisions.push_back({hostname, task_to_schedule, execution_option});
        }
        return decisions;
    }
}
