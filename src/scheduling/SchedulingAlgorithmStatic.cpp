#include <iostream>
#include <limits>
#include <cmath>

#include "scheduling/SchedulingAlgorithmStatic.h"
#include "ProbabilityComputation.h"
#include "JobTracker.h"

namespace wrench {

    std::string SchedulingAlgorithmStatic::pick_execution_option(
        ProbabilityComputation* probability_computation,
        const std::map<std::string, std::map<std::string, std::function<double(double, double)>>>& exec_options,
        const double input_data_size,
        const double input_error_level,
        const double remaining_time,
        OptionComparatorFunction* comparator_function,
        const bool minimize) const {

        /* Initialize min_error_level and selected_delta_t to +inf */
        double selected_delta_t;
        double best_option_value = minimize ? std::numeric_limits<double>::infinity() : -std::numeric_limits<double>::infinity();

        std::string best_option;

        for (const auto& [option_name, option_functions] : exec_options) {
            /* Grab all the necessary info about the execution option */
            const auto exec_option_name = option_name;
            const auto exec_option_time = option_functions.at("t_function")(input_data_size, input_error_level);
            const auto exec_option_data = option_functions.at("d_function")(input_data_size, input_error_level);
            // std::cerr << "LOOKING AT OPTION_NAME = " << exec_option_name << std::endl;

            const double exec_option_time_total = (input_data_size / _io_read_bandwidth) + exec_option_time + (exec_option_data / _io_write_bandwidth);
            /* Select a delta based on the scheme */
            if (_delta_t_scheme == "fixed") {
                // _delta_t_parameter is our fixed delta_t value
                selected_delta_t = _delta_t_parameter;
            } else if (_delta_t_scheme == "compute") {
                selected_delta_t = probability_computation->compute_best_deltat(
                exec_option_time_total, remaining_time, _delta_t_parameter);
            } else {
                throw std::invalid_argument("Unknown delta_t_scheme '" + _delta_t_scheme + "'");
            }
            probability_computation->set_delta_t(selected_delta_t);

            const double comp_value = comparator_function->comp_value(probability_computation, option_functions, input_data_size, input_error_level,
                                                                     remaining_time);

            /* Take the minimum expected error of all the execution options */
            if ((minimize && comp_value < best_option_value) || (!minimize && comp_value > best_option_value)) {
                best_option_value = comp_value;
                best_option = option_name;
            }
        }

        // std::cerr << "STATIC DECISION: " << best_option << std::endl;
        return best_option;
    }

    std::vector<SchedulingAlgorithm::SchedulingDecision> SchedulingAlgorithmStatic::make_decisions(
        JobTracker* job_tracker,
        ProbabilityComputation* probability_computation,
        const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
        const std::string& task_to_schedule,
        const double input_data_size,
        const double input_error_level,
        const double remaining_time,
        OptionComparatorFunction* comparator_function,
        const bool minimize
    ) {
        std::vector<SchedulingAlgorithm::SchedulingDecision> decisions;

        static std::map<std::string, std::string> task_exec_option_decisions;
        if (task_exec_option_decisions.find(task_to_schedule) == task_exec_option_decisions.end()) {
            task_exec_option_decisions[task_to_schedule] = this->pick_execution_option(
                probability_computation,
                exec_options.at(task_to_schedule),
                input_data_size,
                input_error_level,
                remaining_time,
                comparator_function,
                minimize
            );
        }

        // Assign decision for each idle host
        for (const auto& [hostname, job] : *job_tracker) {
            if (job) continue; // Host is busy
            decisions.push_back({hostname, task_to_schedule, task_exec_option_decisions[task_to_schedule]});
        }
        return decisions;
    }
}
