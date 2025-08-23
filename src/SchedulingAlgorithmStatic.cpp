#include <iostream>
#include <limits>
#include <cmath>

#include "SchedulingAlgorithmStatic.h"
#include "ProbabilityComputation.h"
#include "JobTracker.h"

namespace wrench {

    std::string SchedulingAlgorithmStatic::pick_execution_option(
        ProbabilityComputation* probability_computation,
        const std::map<std::string, std::map<std::string, std::function<double(double, double)>>>& exec_options,
        double input_data_size,
        double input_error_level,
        double remaining_time,
        double restart_overhead,
        double io_read_bandwidth,
        double io_write_bandwidth) const {
        /* Initialize min_error_level to +inf */
        double min_expected_error_level = std::numeric_limits<double>::max();
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

            const double expected_error = probability_success * exec_option_error + (1 - probability_success) * _e_fail;

            /* Take the minimum expected error of all the execution options */
            if (expected_error < min_expected_error_level) {
                min_expected_error_level = expected_error;
                min_execution_option = exec_option_name;
            }
        }

        // std::cerr << "STATIC DECISION: " << min_execution_option << std::endl;
        return min_execution_option;
    }

    std::vector<SchedulingAlgorithm::SchedulingDecision> SchedulingAlgorithmStatic::make_decisions(
        JobTracker* job_tracker,
        ProbabilityComputation* probability_computation,
        const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>
        & exec_options,
        const string& task_to_schedule,
        double input_data_size,
        double input_error_level,
        double remaining_time,
        double restart_overhead,
        double io_read_bandwidth,
        double io_write_bandwidth) {
        std::vector<SchedulingAlgorithm::SchedulingDecision> decisions;

        // Whatever the first decision is, it's always going to be the same one,
        // regardless of the host, etc.
        static std::map<std::string, std::string> task_exec_option_decisions;
        if (task_exec_option_decisions.find(task_to_schedule) == task_exec_option_decisions.end()) {
            task_exec_option_decisions[task_to_schedule] = this->pick_execution_option(
                probability_computation,
                exec_options.at(task_to_schedule),
                input_data_size,
                input_error_level,
                remaining_time,
                restart_overhead,
                io_read_bandwidth,
                io_write_bandwidth);
        }

        // Make a decision for each host that's currently idle
        for (const auto& [hostname, job] : *job_tracker) {
            if (job) continue; // Host is not idle
            decisions.push_back({hostname, task_to_schedule, task_exec_option_decisions[task_to_schedule]});
        }
        return decisions;
    }
}
