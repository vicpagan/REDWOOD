#include <iostream>
#include <limits>
#include <cmath>

#include "scheduling/SchedulingAlgorithmGreedyNearsighted.h"
#include "ApplicationSpecs.h"
#include "ProbabilityComputation.h"
#include "Utils.h"

namespace wrench {

    void SchedulingAlgorithmGreedyNearsighted::preprocess_host_decisions(
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

    double SchedulingAlgorithmGreedyNearsighted::calculate_expected_error(
        const std::map<std::string, double> &ps_by_option,
        const std::map<std::string, double> &el_by_option) const {

        double p_all_fail = 1.0;
        double weighted_sum = 0.0;
        double running_prod = 1.0;

        for (const auto &option : _list_of_options) {
            const double p_one_succeeds = 1.0 - std::pow(1.0 - ps_by_option.at(option), _nodes_per_initial_option_decision.at(option));

            weighted_sum += el_by_option.at(option) * p_one_succeeds * running_prod;
            running_prod *= (1.0 - p_one_succeeds);
            p_all_fail   *= (1.0 - p_one_succeeds);
        }
        return weighted_sum + _e_fail * p_all_fail;
    }

    void SchedulingAlgorithmGreedyNearsighted::translate_to_static_decisions(SystemState* system_state_tracker) {
        auto hostname_iterator = system_state_tracker->begin();
        if (hostname_iterator == system_state_tracker->end()) {
            throw std::runtime_error("translate_to_static_decisions: system_state_tracker has no hosts");
        }
        std::string current_hostname;

        for (const auto& [option, num_nodes] : _nodes_per_initial_option_decision) {
            for (int i = 0; i < num_nodes; i++) {
                if (hostname_iterator == system_state_tracker->end()) {
                    throw std::runtime_error(
                        "translate_to_static_decisions: ran out of hosts -- "
                        "total decided node count exceeds system_state_tracker's host count"
                    );
                }
                current_hostname = hostname_iterator->first;
                _static_decisions_per_host[current_hostname][_application_specs->get_task(0)] = option;
                ++hostname_iterator;
            }
        }
    }

}
