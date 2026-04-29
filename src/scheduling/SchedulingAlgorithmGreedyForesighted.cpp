#include <iostream>
#include <limits>
#include <cmath>

#include "scheduling/SchedulingAlgorithmGreedyForesighted.h"
#include "ApplicationSpecs.h"
#include "ProbabilityComputation.h"
#include "Utils.h"

namespace wrench {

    void SchedulingAlgorithmGreedyForesighted::collect_combinations(
        const ApplicationSpecs::ExecOptionDecisionNode *node,
        std::vector<std::string> &current_path) {

        if (node->is_leaf) {
            _all_combinations.push_back(current_path);
            return;
        }

        for (const auto &child : node->children) {
            current_path.push_back(child->execution_option);
            collect_combinations(child.get(), current_path);
            current_path.pop_back();
        }
    }

    double SchedulingAlgorithmGreedyForesighted::calculate_prob_success_one_host(
        int remaining_tasks,
        int task_index,
        double running_input_data_size,
        double running_input_error_level,
        double selected_delta_t,
        std::map<const ApplicationSpecs::ExecOptionDecisionNode*, std::vector<double>> &dp,
        const ApplicationSpecs::ExecOptionDecisionNode* current_task_node,
        const std::vector<std::string> &combo,
        const long n, const long R,
        const long deadline,
        const bool lower_bound) const {

        if (dp.find(current_task_node) == dp.end()) {
            dp[current_task_node] = std::vector<double>(deadline + 1, -1.0);
        }

        if (dp[current_task_node][n] >= 0.0) {
            return dp[current_task_node][n];
        }

        const std::string task_name = _application_specs->get_task(task_index);
        const std::string& option_name = combo[task_index];

        const ApplicationSpecs::ExecOptionDecisionNode* current_child_node = nullptr;
        for (const auto &child : current_task_node->children) {
            if (child->execution_option == option_name) {
                current_child_node = child.get();
                break;
            }
        }

        auto option_functions = _exec_options.at(task_name).at(option_name);

#if OPTIMISTIC_EXECUTION
        const long exec_time = static_cast<long>(std::floor(
            ((running_input_data_size / _io_read_bandwidth_per_node)
            + option_functions.at("t_function")(running_input_data_size, running_input_error_level)
            + (option_functions.at("d_function")(running_input_data_size, running_input_error_level)
               / _io_write_bandwidth_per_node)) / selected_delta_t));
#else
        const long exec_time = ceiling_division(
            ((running_input_data_size / _io_read_bandwidth_per_node)
            + option_functions.at("t_function")(running_input_data_size, running_input_error_level)
            + (option_functions.at("d_function")(running_input_data_size, running_input_error_level)
               / _io_write_bandwidth_per_node)),
            selected_delta_t);
#endif

        double probability;

        if (n < exec_time) {
            probability = 0.0;
        } else {
            double updated_input_size  = option_functions.at("d_function")(running_input_data_size, running_input_error_level);
            double updated_error_level = option_functions.at("e_function")(running_input_data_size, running_input_error_level);

            if (remaining_tasks == 0) {
                probability = _probability_computation->success_probability(exec_time) * 1.0;
            } else {
                double next_task_probability = calculate_prob_success_one_host(
                    remaining_tasks - 1,
                    task_index + 1,
                    updated_input_size,
                    updated_error_level,
                    selected_delta_t,
                    dp,
                    current_child_node,
                    combo,
                    n - exec_time,
                    R, deadline, lower_bound
                );
                probability = _probability_computation->success_probability(exec_time) * next_task_probability;
            }

            for (long u = 0; u < exec_time; u++) {
                long remaining_time_after_failure;
                if (lower_bound) {
                    if (u == 0) {
                        remaining_time_after_failure = (R == 0)
                            ? std::max(n - 1, 0L)
                            : std::max(n - R, 0L);
                    } else {
                        remaining_time_after_failure = std::max(n - u - R, 0L);
                    }
                } else {
                    remaining_time_after_failure = std::max(n - u - R - 1, 0L);
                }

                probability += _probability_computation->fail_probability(u) * calculate_prob_success_one_host(
                    remaining_tasks,
                    task_index,
                    running_input_data_size,
                    running_input_error_level,
                    selected_delta_t,
                    dp,
                    current_task_node,
                    combo,
                    remaining_time_after_failure,
                    R, deadline, lower_bound
                );
            }
        }

        dp[current_task_node][n] = probability;
        return probability;
    }

    double SchedulingAlgorithmGreedyForesighted::calculate_error_level_one_host(
        const ApplicationSpecs::ExecOptionDecisionNode* current_node,
        const std::vector<std::string> &combo,
        int task_index) const {

        if (current_node->is_leaf) {
            return current_node->cumulative_error_factor;
        }

        for (const auto &child : current_node->children) {
            if (child->execution_option == combo[task_index]) {
                return calculate_error_level_one_host(child.get(), combo, task_index + 1);
            }
        }

        throw std::runtime_error("No matching child found for option: " + combo[task_index]);
    }

    double SchedulingAlgorithmGreedyForesighted::calculate_expected_error(
        const std::map<std::vector<std::string>, double> &ps_by_combo,
        const std::map<std::vector<std::string>, double> &el_by_combo) const {

        double p_all_fail = 1.0;
        double weighted_sum = 0.0;
        double running_prod = 1.0;

        for (const auto &combo : _all_combinations) {
            const double p_one_succeeds = 1.0 - std::pow(1.0 - ps_by_combo.at(combo), _nodes_per_combo_decision.at(combo));

            weighted_sum += el_by_combo.at(combo) * p_one_succeeds * running_prod;
            running_prod *= (1.0 - p_one_succeeds);
            p_all_fail   *= (1.0 - p_one_succeeds);
        }
        return weighted_sum + _e_fail * p_all_fail;
    }

    std::vector<SchedulingAlgorithm::SchedulingDecision> SchedulingAlgorithmGreedyForesighted::make_decisions(
        SystemState* system_state_tracker,
        const std::string& task_to_schedule,
        const double input_data_size,
        const double input_error_level,
        const double remaining_time) {

        std::vector<SchedulingDecision> decisions;
        for (const auto& entry : *system_state_tracker) {
            std::string hostname = entry.first;

            if (_static_decisions_per_node.find(hostname) == _static_decisions_per_node.end()) {
                preprocess_decisions(input_data_size, input_error_level, remaining_time, true);

                // for (const auto &[combo, num_nodes] : _nodes_per_combo_decision) {
                //     std::cout << "Combo :";
                //     for (const auto &option : combo) {
                //         std::cout << option << ", ";
                //     }
                //     std::cout << " -------- num_nodes = " << num_nodes << std::endl;
                // }

                // Transform _nodes_per_option_decision into _static_decisions_per_node
                auto hostname_iterator = system_state_tracker->begin();
                std::string current_hostname = hostname_iterator->first;
                for (const auto& [combo, num_nodes] : _nodes_per_combo_decision) {
                    for (int i = 0; i < num_nodes; i++) {
                        int task_idx = 0;
                        for (const auto& option : combo) {
                            _static_decisions_per_node[current_hostname][_application_specs->get_task(task_idx)] = option;
                            task_idx++;
                        }
                        ++hostname_iterator;
                        current_hostname = hostname_iterator->first;
                    }
                }
            }

            if (!system_state_tracker->is_host_idle(hostname)) continue; // Host is not idle

            std::string chosen_option = _static_decisions_per_node.at(hostname).at(task_to_schedule);

            // std::cout << "Selected execution_option " << chosen_option << " for task " << task_to_schedule <<
            //     " on host " << hostname << " with remaining time " << remaining_time << std::endl;
            decisions.push_back({hostname, task_to_schedule, chosen_option});
        }
        return decisions;
    }


}
