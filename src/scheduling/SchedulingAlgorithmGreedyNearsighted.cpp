#include <iostream>
#include <limits>
#include <cmath>

#include "scheduling/SchedulingAlgorithmGreedyNearsighted.h"
#include "ApplicationSpecs.h"
#include "ProbabilityComputation.h"
#include "Utils.h"

namespace wrench {

    void SchedulingAlgorithmGreedyNearsighted::preprocess_decisions(
        const double initial_data_size,
        const double initial_error_level,
        const double deadline,
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

    std::vector<SchedulingAlgorithm::SchedulingDecision> SchedulingAlgorithmGreedyNearsighted::make_decisions(
        SystemState* system_state_tracker,
        const std::string& task_to_schedule,
        const double input_data_size,
        const double input_error_level,
        const double remaining_time) {

        if (_static_decisions_per_node.empty()) {
            preprocess_decisions(input_data_size, input_error_level, remaining_time, true);
            initial_decisions(input_data_size, input_error_level, remaining_time, true);

            // for (const auto& [option, num_nodes] : _nodes_per_initial_option_decision) {
            //     std::cout << "Option " << option << " has " << num_nodes << " nodes" << std::endl;
            // }

            // Transform _nodes_per_option_decision into _static_decisions_per_node
            auto hostname_iterator = system_state_tracker->begin();
            std::string current_hostname = hostname_iterator->first;
            for (const auto& [option, num_nodes] : _nodes_per_initial_option_decision) {
                for (int i = 0; i < num_nodes; i++) {
                    _static_decisions_per_node[current_hostname][_application_specs->get_task(0)] = option;
                    ++hostname_iterator;
                    current_hostname = hostname_iterator->first;
                }
            }

            // for (const auto& [hostname, task] : _static_decisions_per_node) {
            //     for (const auto& [taskname, option] : task) {
            //         std::cout << "Task " << taskname << " has decision option " << option << " for hostname " << hostname << std::endl;
            //     }
            // }
        }

        std::cout << "making decisions for scheduling\n";

        std::vector<SchedulingDecision> decisions;
        for (const auto& entry : *system_state_tracker) {
            std::string hostname = entry.first;

            if (!system_state_tracker->is_host_idle(hostname)) {
                std::cout << "Host " << hostname << " is not idle" << std::endl;
                continue; // Host is not idle
            }

            auto current_decision_node = system_state_tracker->get_host_current_decision_node(hostname);
            if (current_decision_node == nullptr) {
                current_decision_node = _application_specs->get_decision_tree_root();
            }
            auto compute_nodes_task_to_schedule = current_decision_node->children[0]->task; // Should never activate for leaf nodes

            if (_static_decisions_per_node.at(hostname).find(compute_nodes_task_to_schedule) == _static_decisions_per_node.at(hostname).end()) {
                bool minimize = _comparator_function->is_minimizing();

                std::string best_option;
                double best_value = minimize
                                    ? std::numeric_limits<double>::infinity()
                                    : -std::numeric_limits<double>::infinity();

                for (const auto &child : current_decision_node->children) {
                    const std::string &option_name = child->execution_option;
                    const auto &option_functions = _exec_options.at(compute_nodes_task_to_schedule).at(option_name);

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

                _static_decisions_per_node[hostname][compute_nodes_task_to_schedule] = best_option;
            }
            std::string chosen_option = _static_decisions_per_node.at(hostname).at(compute_nodes_task_to_schedule);

            std::cout << "Selected execution_option " << chosen_option << " for task " << compute_nodes_task_to_schedule <<
                " on host " << hostname << " with remaining time " << remaining_time << std::endl;
            decisions.push_back({hostname, compute_nodes_task_to_schedule, chosen_option});

        }
        return decisions;
    }


}
