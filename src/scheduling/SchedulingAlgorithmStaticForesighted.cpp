#include <iostream>
#include <limits>
#include <cmath>
#include <future>

#include "scheduling/SchedulingAlgorithmStaticForesighted.h"
#include "ApplicationSpecs.h"
#include "ProbabilityComputation.h"
#include "Utils.h"

namespace wrench {

    void SchedulingAlgorithmStaticForesighted::preprocess_decisions(
        const double initial_data_size,
        const double initial_error_level,
        const double remaining_time,
        const bool lower_bound) {

        if (_delta_t_scheme == "fixed") {
            _delta_t = _delta_t_parameter;
        } else {
            throw std::invalid_argument("Static foresighted does not support 'compute' delta_t_scheme");
        }
        _probability_computation->set_delta_t(_delta_t);

#if OPTIMISTIC_EXECUTION
        const auto n = static_cast<long>(std::floor(remaining_time / _delta_t));
        const auto R = static_cast<long>(std::floor(_application_specs->get_restart_overhead() / _delta_t));
#else
        const auto n = ceiling_division(remaining_time, _delta_t);
        const auto R = ceiling_division(_application_specs->get_restart_overhead(), _delta_t);
#endif

        const bool minimize = _comparator_function->is_minimizing();

        std::vector<std::string> current_path;
        collect_combinations(_application_specs->get_decision_tree_root(), current_path);

        std::vector<std::future<double>> futures;
        futures.reserve(_all_combinations.size());

        for (const auto &combo : _all_combinations) {
            futures.push_back(std::async(std::launch::async, [&, combo]() {
                const int num_tasks = static_cast<int>(combo.size()) - 1;
                std::map<const ApplicationSpecs::ExecOptionDecisionNode*, std::vector<double>> dp;

                if (dynamic_cast<ExpectedErrorComparator*>(_comparator_function)) {
                    return calculate_expected_error(
                        num_tasks, 0, initial_data_size, initial_error_level,
                        _delta_t, dp, _application_specs->get_decision_tree_root(),
                        combo, n, R, n, lower_bound);
                } else if (dynamic_cast<ProbabilitySuccessComparator*>(_comparator_function)) {
                    return calculate_prob_success(
                        num_tasks, 0, initial_data_size, initial_error_level,
                        _delta_t, dp, _application_specs->get_decision_tree_root(),
                        combo, n, R, n, lower_bound);
                } else if (dynamic_cast<ErrorLevelComparator*>(_comparator_function)) {
                    return calculate_error_level(
                        _application_specs->get_decision_tree_root(), combo, 0);
                } else if (dynamic_cast<SuccessErrorRatioComparator*>(_comparator_function)) {
                    std::map<const ApplicationSpecs::ExecOptionDecisionNode*, std::vector<double>> dp2;
                    double ps = calculate_prob_success(
                        num_tasks, 0, initial_data_size, initial_error_level,
                        _delta_t, dp2, _application_specs->get_decision_tree_root(),
                        combo, n, R, n, lower_bound);
                    double el = calculate_error_level(
                        _application_specs->get_decision_tree_root(), combo, 0);
                    return ps / el;
                } else {
                    throw std::runtime_error("Unknown comparator type");
                }
            }));
        }

        // Collect results and find best
        std::vector<std::string> best_combo;
        double best_value = minimize ? std::numeric_limits<double>::infinity()
                                     : -std::numeric_limits<double>::infinity();

        for (size_t i = 0; i < _all_combinations.size(); i++) {
            double value = futures[i].get();
            bool better = minimize ? (value < best_value) : (value > best_value);
            if (better) {
                best_value = value;
                best_combo = _all_combinations[i];
            }
        }

        for (int i = 0; i < static_cast<int>(best_combo.size()); i++) {
            _static_decisions[_application_specs->get_task(i)] = best_combo[i];
        }
    }

    void SchedulingAlgorithmStaticForesighted::collect_combinations(
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

    double SchedulingAlgorithmStaticForesighted::calculate_prob_success(
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
                double next_task_probability = calculate_prob_success(
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

                probability += _probability_computation->fail_probability(u) * calculate_prob_success(
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

    double SchedulingAlgorithmStaticForesighted::calculate_expected_error(
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

        // Find the child node matching the chosen option
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

        double expected_error;

        if (n < exec_time) {
            expected_error = _application_specs->get_e_fail();
        } else {
            double updated_input_size  = option_functions.at("d_function")(running_input_data_size, running_input_error_level);
            double updated_error_level = option_functions.at("e_function")(running_input_data_size, running_input_error_level);

            if (remaining_tasks == 0) {
                expected_error = _probability_computation->success_probability(exec_time) * updated_error_level;
            } else {
                double next_task_error = calculate_expected_error(
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
                expected_error = _probability_computation->success_probability(exec_time) * next_task_error;
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

                expected_error += _probability_computation->fail_probability(u) * calculate_expected_error(
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

        dp[current_task_node][n] = expected_error;
        return expected_error;
    }

    double SchedulingAlgorithmStaticForesighted::calculate_error_level(
        const ApplicationSpecs::ExecOptionDecisionNode* current_node,
        const std::vector<std::string> &combo,
        int task_index) const {

        if (current_node->is_leaf) {
            return current_node->cumulative_error_factor;
        }

        for (const auto &child : current_node->children) {
            if (child->execution_option == combo[task_index]) {
                return calculate_error_level(child.get(), combo, task_index + 1);
            }
        }

        throw std::runtime_error("No matching child found for option: " + combo[task_index]);
    }


}
