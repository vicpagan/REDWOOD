#include <iostream>
#include <limits>
#include <cmath>

#include "scheduling/SchedulingAlgorithmDynamic.h"
#include "ProbabilityComputation.h"
#include "JobTracker.h"

namespace wrench {

    /**
     * @brief Calculates minimum expected error with its corresponding execution option decision iteratively
     *        for each execution option for a single task
     *
     * @param exec_option_metrics This is a list of the function metrics for each execution option
     * @param probability_failures This is the list of our p_us for each execution option
     * @param probability_success This is our e^(-lambda * m_j * delta) for each execution option
     * @param m_j This is m_j in the paper for each execution option
     * @param n This is n in the paper
     * @param R this is R in the paper
     * @return The expected error for the selected execution option
     */
    std::pair<std::string, double> SchedulingAlgorithmDynamic::calculate_expected_error(
        const std::map<std::string, std::map<std::string, double>> &exec_option_metrics,
        const std::map<std::string, double> &probability_success,
        const std::map<std::string, std::vector<double> > &probability_failures,
        const std::map<std::string, long> &m_j,
        const long n, const long R) const {

        std::vector<std::pair<std::string, double>> dp(n+1, std::make_pair("", 0.0));

        for (int i = 0; i <= n; i++) {
            std::string best_option;
            double min_expected_error = std::numeric_limits<double>::infinity();
            for (const auto &[option_name, option_functions]: exec_option_metrics) {
                double expected_error;
                if (i < m_j.at(option_name)) {
                    expected_error = _e_fail;
                } else {
                    expected_error = probability_success.at(option_name) * option_functions.at("e_function");
                    for (long u = 0; u < m_j.at(option_name); u++) {
                        expected_error += probability_failures.at(option_name).at(u) * dp[std::max(i - u - R - 1,0L)].second;
                    }
                }
                if (expected_error < min_expected_error) {
                    min_expected_error = expected_error;
                    best_option = option_name;
                }
            }
            dp[i] = std::make_pair(best_option, min_expected_error);
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
     * @return The name of the best execution option
     */
    std::string SchedulingAlgorithmDynamic::pick_execution_option(
        ProbabilityComputation* probability_computation,
        const std::map<std::string, std::map<std::string, std::function<double(double, double)>>>& exec_options,
        const double input_data_size,
        const double input_error_level,
        const double remaining_time) const {
        /* Initialize selected_delta_t to +inf */
        double selected_delta_t = std::numeric_limits<double>::max();
        std::map<std::string, std::map<std::string, double>> exec_option_metrics;
        const double lambda = probability_computation->get_lambda();
        for (const auto& [option_name, option_functions] : exec_options) {
            /* Grab all the necessary info about the execution option */
            exec_option_metrics[option_name]["t_function"] = option_functions.at("t_function")(input_data_size, input_error_level);
            exec_option_metrics[option_name]["d_function"] = option_functions.at("d_function")(input_data_size, input_error_level);
            exec_option_metrics[option_name]["e_function"] = option_functions.at("e_function")(input_data_size, input_error_level);
            // std::cerr << "LOOKING AT OPTION_NAME = " << option_name << std::endl;

            /* Select a delta based on the scheme */
            if (_delta_t_scheme == "fixed") {
                // _delta_t_parameter is our fixed delta_t value
                selected_delta_t = _delta_t_parameter;
            } else if (_delta_t_scheme == "compute") {
                // _delta_t_parameter is our precision for calculating a good enough delta_t
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
        std::map<std::string, std::vector<double> > probability_failures;
        for (const auto &[option_name, option_functions]: exec_options) {
            m_j[option_name] = static_cast<long>(std::ceil(
                ((input_data_size / _io_read_bandwidth) + exec_option_metrics.at(option_name).at("t_function") +
                (exec_option_metrics.at(option_name).at("d_function") / _io_write_bandwidth)) /
                selected_delta_t));

            /* Precalculate probability of success and the list of failure probabilities for each possible failure point in execution */
            probability_success[option_name] = exp(-lambda * static_cast<double>(m_j.at(option_name)) * selected_delta_t);
            probability_failures[option_name].resize(m_j.at(option_name));
            for (long u = 0; u < m_j.at(option_name); u++) {
                probability_failures[option_name][u] = exp(-lambda * static_cast<double>(u) * selected_delta_t) -
                    exp(-lambda * static_cast<double>((u + 1)) * selected_delta_t);
            }
        }
        const auto n = static_cast<long>(std::ceil(remaining_time / selected_delta_t));
        const auto R = static_cast<long>(std::ceil(_restart_overhead / selected_delta_t));

        std::pair<std::string, double> best_option = calculate_expected_error(exec_option_metrics, probability_success, probability_failures,
                                                   m_j, n, R);

        std::cout<< "Best option is " << best_option.first << " with expected error " << best_option.second << std::endl;
        std::string min_execution_option = best_option.first;
        std::cerr << "DYNAMIC DECISION: " << min_execution_option << std::endl;
        return min_execution_option;
    }

    std::vector<SchedulingAlgorithm::SchedulingDecision> SchedulingAlgorithmDynamic::make_decisions(
        JobTracker* job_tracker,
        ProbabilityComputation* probability_computation,
        const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
        const std::string& task_to_schedule,
        const double input_data_size,
        const double input_error_level,
        const double remaining_time,
        OptionComparatorFunction* comparator_function,
        const bool minimize) {
        std::vector<SchedulingDecision> decisions;

        // Make a decision for each host that's currently idle
        // All these decisions are independent so that makes it easy
        for (const auto& [hostname, job] : *job_tracker) {
            if (job) continue; // Host is not idle

            const auto execution_option = this->pick_execution_option(
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
