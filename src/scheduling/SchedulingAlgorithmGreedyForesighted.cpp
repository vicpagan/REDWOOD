#include <iostream>
#include <limits>
#include <cmath>

#include "scheduling/SchedulingAlgorithmGreedyForesighted.h"
#include "ApplicationSpecs.h"
#include "ProbabilityComputation.h"
#include "Utils.h"

namespace wrench {

    void SchedulingAlgorithmGreedyForesighted::collect_combinations(
        std::vector<std::vector<std::string>> &all_combinations,
        const ApplicationSpecs::ExecOptionDecisionNode *node,
        std::vector<std::string> &current_path) {

        if (node->is_leaf) {
            all_combinations.push_back(current_path);
            return;
        }

        for (const auto &child : node->children) {
            current_path.push_back(child->execution_option);
            collect_combinations(all_combinations, child.get(), current_path);
            current_path.pop_back();
        }
    }

    void SchedulingAlgorithmGreedyForesighted::preprocess_host_decisions(
        const std::string& hostname,
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

        const long d = lower_bound ?
            ceiling_division(deadline, _delta_t) :
            floor_division(deadline, _delta_t);
        const long R = lower_bound ?
            floor_division(_application_specs->get_restart_overhead(), _delta_t) :
            ceiling_division(_application_specs->get_restart_overhead(), _delta_t);

        const int num_tasks = static_cast<int>(_exec_options.size()) - 1;
        const int task_to_schedule_index = _application_specs->get_host_task_to_schedule_index(hostname);
        const ApplicationSpecs::ExecOptionDecisionNode *current_decision_node = _application_specs->get_host_current_decision_node(hostname);

        // initialize list of combinations
        std::vector<std::vector<std::string>> all_combinations;
        std::vector<std::string> current_path;
        collect_combinations(all_combinations, current_decision_node, current_path);

        // sort list of combinations by error level
        std::map<std::vector<std::string>, double> el_by_combo;
        for (const auto &combo : all_combinations) {
            el_by_combo.emplace(combo, calculate_error_level_one_host(current_decision_node, combo, 0));
        }
        std::sort(all_combinations.begin(), all_combinations.end(), [&](const auto &a, const auto &b) {
            return el_by_combo.at(a) < el_by_combo.at(b);
        });

        // Select combo with the highest probability of success
        // Schedules this combo on all compute nodes
        double best_value;
        std::vector<std::string> best_combo;

        if (dynamic_cast<ProbabilitySuccessComparator*>(_comparator_function)) {
            best_value = -std::numeric_limits<double>::infinity();

            std::map<const ApplicationSpecs::ExecOptionDecisionNode*, std::vector<double>> dp;
            for (const auto &combo : all_combinations) {

                double ps = calculate_prob_success_one_host(
                    num_tasks - task_to_schedule_index, task_to_schedule_index, initial_data_size, initial_error_level,
                    _delta_t, dp, current_decision_node,
                    combo, 0,
                    d, R, d, lower_bound);
                dp.clear();

                if (ps > best_value) {
                    best_value = ps;
                    best_combo = combo;
                }
            }
        }
        // Schedules based on the lowest expected error of any combination of option paths (combination of combinations lol)
        else if (dynamic_cast<ExpectedErrorComparator*>(_comparator_function)) {
            best_value = std::numeric_limits<double>::infinity();

            std::map<const ApplicationSpecs::ExecOptionDecisionNode*, std::vector<double>> dp;
            for (const auto &combo : all_combinations) {

                double ps = calculate_prob_success_one_host(
                    num_tasks - task_to_schedule_index, task_to_schedule_index, initial_data_size, initial_error_level,
                    _delta_t, dp, current_decision_node,
                    combo, 0,
                    d, R, d, lower_bound);
                dp.clear();

                double exp_err = el_by_combo.at(combo) * ps + _e_fail * (1.0 - ps);

                if (exp_err < best_value) {
                    best_value = exp_err;
                    best_combo = combo;
                }
            }
        }
        //
        else if (dynamic_cast<SuccessErrorRatioComparator*>(_comparator_function)) {
            best_value = -std::numeric_limits<double>::infinity();

            std::map<const ApplicationSpecs::ExecOptionDecisionNode*, std::vector<double>> dp;
            for (const auto &combo : all_combinations) {

                double ps = calculate_prob_success_one_host(
                    num_tasks - task_to_schedule_index, task_to_schedule_index, initial_data_size, initial_error_level,
                    _delta_t, dp, current_decision_node,
                    combo, 0,
                    d, R, d, lower_bound);
                dp.clear();
                double el = el_by_combo[combo];

                double ps_el_ratio = ps / el;

                if (ps_el_ratio > best_value) {
                    best_value = ps_el_ratio;
                    best_combo = combo;
                }
            }
        }
        else if (auto error_level_comparator_function = dynamic_cast<ErrorLevelComparator*>(_comparator_function)) {
            best_value = std::numeric_limits<double>::infinity();
            double ps_threshold = error_level_comparator_function->get_prob_success_threshold();

            std::map<const ApplicationSpecs::ExecOptionDecisionNode*, std::vector<double>> dp;
            for (const auto &combo : all_combinations) {

                double ps = calculate_prob_success_one_host(
                    num_tasks - task_to_schedule_index, task_to_schedule_index, initial_data_size, initial_error_level,
                    _delta_t, dp, current_decision_node,
                    combo, 0,
                    d, R, d, lower_bound);
                dp.clear();
                double el = el_by_combo[combo];

                if (ps > ps_threshold) {
                    if (el < best_value) {
                        best_value = el;
                        best_combo = combo;
                    }
                }
            }

            // if theres no valid combo above the threshold, just choose the lowest error level
            if (best_combo.empty()) {
                best_combo = all_combinations.at(0);
            }
        }
        else {
            throw std::runtime_error("Unknown comparator type");
        }

        std::cout << "best_combo size = " << best_combo.size()
          << " for host " << hostname << std::endl;

        int task_idx = task_to_schedule_index;
        for (const auto &option : best_combo) {
            _static_decisions_per_host[hostname][_application_specs->get_task(task_idx++)] = option;
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
        int relative_task_index,
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
        const std::string& option_name = combo[relative_task_index];

        const ApplicationSpecs::ExecOptionDecisionNode* current_child_node = nullptr;
        for (const auto &child : current_task_node->children) {
            if (child->execution_option == option_name) {
                current_child_node = child.get();
                break;
            }
        }

        auto option_functions = _exec_options.at(task_name).at(option_name);

        long exec_time;
        if (lower_bound) {
            exec_time = floor_division(
                ((running_input_data_size / _io_read_bandwidth_per_node)
                + option_functions.at("t_function")(running_input_data_size, running_input_error_level)
                + (option_functions.at("d_function")(running_input_data_size, running_input_error_level)
                   / _io_write_bandwidth_per_node)),
                selected_delta_t);
        } else {
            exec_time = ceiling_division(
                ((running_input_data_size / _io_read_bandwidth_per_node)
                + option_functions.at("t_function")(running_input_data_size, running_input_error_level)
                + (option_functions.at("d_function")(running_input_data_size, running_input_error_level)
                   / _io_write_bandwidth_per_node)),
                selected_delta_t);
        }

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
                    relative_task_index + 1,
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
                    relative_task_index,
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
        int relative_task_index) const {

        if (current_node->is_leaf) {
            return current_node->cumulative_error_factor;
        }

        for (const auto &child : current_node->children) {
            if (child->execution_option == combo[relative_task_index]) {
                return calculate_error_level_one_host(child.get(), combo, relative_task_index + 1);
            }
        }

        throw std::runtime_error("No matching child found for option: " + combo[relative_task_index]);
    }

    double SchedulingAlgorithmGreedyForesighted::calculate_expected_error(
        const std::vector<std::vector<std::string>> &all_combinations,
        const std::map<std::vector<std::string>, double> &ps_by_combo,
        const std::map<std::vector<std::string>, double> &el_by_combo) const {

        double p_all_fail = 1.0;
        double weighted_sum = 0.0;
        double running_prod = 1.0;

        for (const auto &combo : all_combinations) {
            const double p_one_succeeds = 1.0 - std::pow(1.0 - ps_by_combo.at(combo), _nodes_per_combo_decision.at(combo));

            weighted_sum += el_by_combo.at(combo) * p_one_succeeds * running_prod;
            running_prod *= (1.0 - p_one_succeeds);
            p_all_fail   *= (1.0 - p_one_succeeds);
        }
        return weighted_sum + _e_fail * p_all_fail;
    }

    //FIXME: Does not properly handle when scheduling every host on a task that is NOT the first task
    void SchedulingAlgorithmGreedyForesighted::translate_to_static_decisions(SystemState* system_state_tracker) {

        // Transform _nodes_per_option_decision into _static_decisions_per_host
        auto hostname_iterator = system_state_tracker->begin();
        std::string current_hostname = hostname_iterator->first;
        for (const auto& [combo, num_nodes] : _nodes_per_combo_decision) {
            for (int i = 0; i < num_nodes; i++) {
                int task_idx = 0;
                for (const auto& option : combo) {
                    _static_decisions_per_host[current_hostname][_application_specs->get_task(task_idx)] = option;
                    task_idx++;
                }
                ++hostname_iterator;
                current_hostname = hostname_iterator->first;
            }
        }
    }

}
