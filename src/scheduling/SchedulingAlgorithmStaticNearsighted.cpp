#include <iostream>
#include <limits>
#include <cmath>

#include "scheduling/SchedulingAlgorithmStaticNearsighted.h"
#include "ProbabilityComputation.h"

namespace wrench {

    void SchedulingAlgorithmStaticNearsighted::preprocess_decisions(
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

    std::vector<SchedulingAlgorithm::SchedulingDecision> SchedulingAlgorithmStaticNearsighted::make_decisions(
        SystemState* system_state_tracker,
        const std::string& task_to_schedule,
        const double input_data_size,
        const double input_error_level,
        const double remaining_time) {

        if (_static_decisions.find(task_to_schedule) == _static_decisions.end()) {

            // Find the node for the current task by following previous decisions
            const ApplicationSpecs::ExecOptionDecisionNode* current_node =
                _application_specs->get_decision_tree_root();

            for (int i = 0; ; i++) {
                const std::string prev_task = _application_specs->get_task(i);
                if (prev_task == task_to_schedule) break;

                const std::string &prev_decision = _static_decisions.at(prev_task);
                for (const auto &child : current_node->children) {
                    if (child->execution_option == prev_decision) {
                        current_node = child.get();
                        break;
                    }
                }
            }

            bool minimize = _comparator_function->is_minimizing();

            std::string best_option;
            double best_value = minimize
                                ? std::numeric_limits<double>::infinity()
                                : -std::numeric_limits<double>::infinity();

            for (const auto &child : current_node->children) {
                const std::string &option_name = child->execution_option;
                const auto &option_functions = _exec_options.at(task_to_schedule).at(option_name);

                double value = _comparator_function->comp_value(
                    _probability_computation,
                    option_functions,
                    input_data_size,
                    input_error_level,
                    remaining_time);

                // std::cout << "Evaluated option " << option_name << " for task " << task_to_schedule <<
                //     " with value " << value << " and remaining time " << remaining_time << std::endl;

                bool better = minimize ? (value < best_value) : (value > best_value);
                if (better) {
                    best_value = value;
                    best_option = option_name;
                }
            }

            // std::cout << "Selected option " << best_option << " for task " << task_to_schedule <<
            //     " with value " << best_value << " and remaining time " << remaining_time << std::endl;

            _static_decisions[task_to_schedule] = best_option;
        }

        const std::string chosen_option = _static_decisions.at(task_to_schedule);

        std::vector<SchedulingDecision> decisions;
        for (const auto& entry : *system_state_tracker) {
            const std::string& hostname = entry.first;
            if (!system_state_tracker->is_host_idle(hostname)) continue;
            decisions.push_back({hostname, task_to_schedule, chosen_option});
        }
        return decisions;
    }
}