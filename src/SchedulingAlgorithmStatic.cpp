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
        double remaining_time) const {
        /* Initialize min_error_level and selected_delta_t to +inf */
        double selected_delta_t = std::numeric_limits<double>::max();
        double min_expected_error_level = std::numeric_limits<double>::max();
        std::string min_execution_option;

        for (const auto& [option_name, option_functions] : exec_options) {
            /* Grab all the necessary info about the execution option */
            const auto exec_option_name = option_name;
            const auto exec_option_time = option_functions.at("t_function")(input_data_size, input_error_level);
            const auto exec_option_data = option_functions.at("d_function")(input_data_size, input_error_level);
            const auto exec_option_error = option_functions.at("e_function")(input_data_size, input_error_level);
            // std::cerr << "LOOKING AT OPTION_NAME = " << exec_option_name << std::endl;

            const double exec_option_time_total = (input_data_size / _io_read_bandwidth) + exec_option_time + (exec_option_data / _io_write_bandwidth);
            /* Select a delta based on the scheme */
            if (_delta_t_scheme == "fixed") {
                // _delta_t_parameter is our fixed delta_t value
                selected_delta_t = _delta_t_parameter;
            } else if (_delta_t_scheme == "compute") {
                selected_delta_t = std::min(selected_delta_t, probability_computation->compute_best_deltat(
                exec_option_time_total, remaining_time, _delta_t_parameter));
            } else {
                throw std::invalid_argument("Unknown delta_t_scheme '" + _delta_t_scheme + "'");
            }
            probability_computation->set_delta_t(selected_delta_t);

            /* Calculate the expected error for the current exec option */
            const auto exec_option_probability_success = probability_computation->compute_probability(exec_option_time_total, remaining_time, false);
            const auto expected_error_option = exec_option_probability_success * exec_option_error +
                (1.0 - exec_option_probability_success) * _e_fail;
            // std::cout << "EXEC OPTION " << exec_option_name << " EXPECTED ERROR: " << expected_error_option << std::endl;

            /* Take the minimum expected error of all the execution options */
            if (expected_error_option < min_expected_error_level) {
                min_expected_error_level = expected_error_option;
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
        double remaining_time) {
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
                remaining_time);
        }

        // Make a decision for each host that's currently idle
        for (const auto& [hostname, job] : *job_tracker) {
            if (job) continue; // Host is not idle
            decisions.push_back({hostname, task_to_schedule, task_exec_option_decisions[task_to_schedule]});
        }
        return decisions;
    }
}
