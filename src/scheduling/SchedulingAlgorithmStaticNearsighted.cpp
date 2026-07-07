#include <iostream>
#include <limits>
#include <cmath>

#include "scheduling/SchedulingAlgorithmStaticNearsighted.h"
#include "ProbabilityComputation.h"

namespace wrench {

    void SchedulingAlgorithmStaticNearsighted::preprocess_host_decisions(
        const std::string& hostname,
        const double input_data_size,
        const double input_error_level,
        const double remaining_time,
        const bool lower_bound) {

        if (_delta_t_scheme == "fixed") {
            _delta_t = _delta_t_parameter;
        }
        else if (_delta_t_scheme == "compute") {
            throw std::invalid_argument("Static foresighted does not support 'compute' delta_t_scheme");
        }
        else {
            throw std::invalid_argument("Unknown delta_t_scheme '" + _delta_t_scheme + "'");
        }
        _probability_computation->set_delta_t(_delta_t);

        bool minimize = _comparator_function->is_minimizing();

        std::string best_option;
        double best_value = minimize
                            ? std::numeric_limits<double>::infinity()
                            : -std::numeric_limits<double>::infinity();

        std::string task_to_schedule = _application_specs->get_host_task_to_schedule(hostname);
        const auto current_decision_node = _application_specs->get_host_current_decision_node(hostname);
        for (const auto& child : current_decision_node->children) {

            std::string option_name = child->execution_option;
            const auto & option_functions = _exec_options.at(task_to_schedule).at(option_name);
            double value = _comparator_function->comp_value(
                _probability_computation,
                option_functions,
                input_data_size,
                input_error_level,
                remaining_time);

            // std::cout << "Evaluated option " << option_name << " for task " << compute_nodes_task_to_schedule <<
            //     " with value " << value << " and remaining time " << remaining_time << std::endl;

            bool better = minimize ? (value < best_value) : (value > best_value);
            if (better) {
                best_value = value;
                best_option = option_name;
            }
        }

        _static_decisions_per_host[hostname][task_to_schedule] = best_option;
    }
}