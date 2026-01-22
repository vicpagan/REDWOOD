#include <iostream>
#include <limits>
#include <cmath>

#include "scheduling/SchedulingAlgorithmStatic.h"
#include "ProbabilityComputation.h"
#include "SystemState.h"

namespace wrench {

    void SchedulingAlgorithmStatic::preprocess_decisions(const double initial_data_size,
        const double initial_error_level,
        const double deadline,
        const bool minimize) {

        // /* Initialize min_error_level and selected_delta_t to +inf */
        // double selected_delta_t;
        // double best_option_value = minimize ? std::numeric_limits<double>::infinity() : -std::numeric_limits<double>::infinity();
        //
        // std::string best_option;
        //
        // for (const auto& [task_name, task_options]: _exec_options) {
        //     for (const auto& [option_name, option_functions] : task_options) {
        //         /* Grab all the necessary info about the execution option */
        //         const auto exec_option_name = option_name;
        //         const auto exec_option_time = option_functions.at("t_function")(initial_data_size, initial_error_level);
        //         const auto exec_option_data = option_functions.at("d_function")(initial_data_size, initial_error_level);
        //         // std::cerr << "LOOKING AT OPTION_NAME = " << exec_option_name << std::endl;
        //
        //         const double exec_option_time_total = (initial_data_size /  _application_specs->get_io_read_bandwidth()) + exec_option_time + (exec_option_data /  _application_specs->get_io_write_bandwidth());
        //         /* Select a delta based on the scheme */
        //         if (_delta_t_scheme == "fixed") {
        //             // _delta_t_parameter is our fixed delta_t value
        //             selected_delta_t = _delta_t_parameter;
        //         } else if (_delta_t_scheme == "compute") {
        //             throw std::invalid_argument("Dynamic scheduling does not support 'compute' delta_t_scheme for multitask yet");
        //         } else {
        //             throw std::invalid_argument("Unknown delta_t_scheme '" + _delta_t_scheme + "'");
        //         }
        //         _probability_computation->set_delta_t(selected_delta_t);
        //
        //         const double comp_value = _comparator_function->comp_value(_probability_computation, option_functions, initial_data_size, initial_error_level,
        //                                                                  deadline);
        //
        //         /* Select the option with the optimal metric were focusing on */
        //         if ((minimize && comp_value < best_option_value) || (!minimize && comp_value > best_option_value)) {
        //             best_option_value = comp_value;
        //             best_option = option_name;
        //         }
        //     }
        // }
    }

    void fill_preprocessing_table(
            double input_data_size,
            double input_error_level,
            double remaining_time,
            bool minimize) {



    }

    std::vector<SchedulingAlgorithm::SchedulingDecision> SchedulingAlgorithmStatic::make_decisions(
        SystemState* system_state_tracker,
        const std::string& task_to_schedule,
        const double remaining_time,
        const bool minimize ) {
        // std::vector<SchedulingAlgorithm::SchedulingDecision> decisions;
        //
        // static std::map<std::string, std::string> task_exec_option_decisions;
        // if (task_exec_option_decisions.find(task_to_schedule) == task_exec_option_decisions.end()) {
        //     task_exec_option_decisions[task_to_schedule] = this->pick_execution_option(
        //         probability_computation,
        //         exec_options.at(task_to_schedule),
        //         input_data_size,
        //         input_error_level,
        //         remaining_time,
        //         comparator_function,
        //         minimize
        //     );
        // }
        //
        // // Assign decision for each idle host
        // for (const auto& entry : *system_state_tracker) {
        //     std::string hostname = entry.first;
        //     if (!system_state_tracker->is_host_idle(hostname)) continue; // Host is busy
        //
        //     decisions.push_back({hostname, task_to_schedule, task_exec_option_decisions[task_to_schedule]});
        // }
        // return decisions;
    }
}
