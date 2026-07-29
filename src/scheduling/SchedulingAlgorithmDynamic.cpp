#include <iostream>
#include <limits>
#include <cmath>

#include "scheduling/SchedulingAlgorithmDynamic.h"
#include "ProbabilityComputation.h"
#include "SystemState.h"
#include "Utils.h"

namespace wrench {
    void SchedulingAlgorithmDynamic::preprocess_host_decisions(const std::string& hostname,
                                                               const double initial_data_size,
                                                               const double initial_error_level,
                                                               const double deadline,
                                                               const bool lower_bound) {
        // Select a delta_t if not already done so
        if (_delta_t == -1) {
            double selected_delta_t;
            if (_delta_t_scheme == "fixed") {
                selected_delta_t = _delta_t_parameter;
            }
            else if (_delta_t_scheme == "compute") {
                throw std::invalid_argument(
                    "Delta T computation unavailable at the moment (under construction sorry!)");
                // selected_delta_t = this->compute_best_delta_t(initial_data_size, initial_error_level, deadline, 1e-2);
            }
            else {
                throw std::invalid_argument("Unknown delta_t_scheme '" + _delta_t_scheme + "'");
            }

            // std::cout << "Selected delta_t: " << selected_delta_t << std::endl;

            _delta_t = selected_delta_t;
            _probability_computation->set_delta_t(selected_delta_t);
        }

        _preprocessed_decisions_by_host.erase(hostname);
        this->fill_host_preprocessing_table(hostname, initial_data_size, initial_error_level, deadline, lower_bound);
    }

    double SchedulingAlgorithmDynamic::calculate_expected_error(
        int remaining_tasks,
        int task_index,
        double running_input_data_size,
        double running_input_error_level,
        double selected_delta_t,
        std::map<const ApplicationSpecs::ExecOptionDecisionNode*, std::vector<std::pair<std::string, double>>>& dp,
        const ApplicationSpecs::ExecOptionDecisionNode* current_task_node,
        const long n, const long R,
        const long deadline,
        const bool lower_bound) const {
        // NOTE: simpler than initializing it all at once which requires tree traversal
        if (dp.find(current_task_node) == dp.end()) {
            dp[current_task_node] = std::vector<std::pair<std::string, double>>(
                deadline + 1,
                std::make_pair("impossible", -1.0)
            );
        }

        if (dp[current_task_node][n] != std::make_pair(std::string("impossible"), -1.0)) {
            return dp[current_task_node][n].second;
        }

        const std::string task_name = _application_specs->get_task(task_index);
        std::string best_option;
        double min_expected_error = std::numeric_limits<double>::infinity();
        // FIXME: Should be e_fail and the simulator should have a way of handling impossible scheduling (give up and dont waste time)

        int num_options = current_task_node->num_children;
        for (int i = 0; i < num_options; i++) {
            auto current_child_node = current_task_node->children[i].get();
            const std::string option_name = current_child_node->execution_option;

            auto option_functions = _exec_options.at(task_name).at(option_name);
            long exec_time;
            if (lower_bound) {
                exec_time = floor_division(
                    ((running_input_data_size / _io_read_bandwidth_per_node) + option_functions.
                        at("t_function")(running_input_data_size, running_input_error_level) + (option_functions.
                            at("d_function")(running_input_data_size, running_input_error_level) /
                            _io_write_bandwidth_per_node)), selected_delta_t);
            }
            else {
                exec_time = ceiling_division(
                    ((running_input_data_size / _io_read_bandwidth_per_node) + option_functions.
                        at("t_function")(running_input_data_size, running_input_error_level) + (option_functions.
                            at("d_function")(running_input_data_size, running_input_error_level) /
                            _io_write_bandwidth_per_node)), selected_delta_t);
            }
            double expected_error;

            if (n < exec_time) {
                expected_error = _application_specs->get_e_fail();
            }
            else {
                double updated_input_size = option_functions.at("d_function")(
                    running_input_data_size, running_input_error_level);
                double updated_error_level = option_functions.at("e_function")(
                    running_input_data_size, running_input_error_level);

                // Task success
                if (remaining_tasks == 0) {
                    expected_error = _probability_computation->success_probability(exec_time) * updated_error_level;
                }
                else {
                    double next_task_error = calculate_expected_error(
                        remaining_tasks - 1,
                        task_index + 1,
                        updated_input_size,
                        updated_error_level,
                        selected_delta_t,
                        dp,
                        current_child_node,
                        n - exec_time,
                        R,
                        deadline,
                        lower_bound
                    );

                    expected_error = _probability_computation->success_probability(exec_time) * next_task_error;
                }

                // Task failure
                for (long u = 0; u < exec_time; u++) {
                    // FIXME: This is gross fuck branching statements
                    long remaining_time_after_failure;
                    if (lower_bound) {
                        if (u == 0) {
                            // Lower bound: waste 0 of current step, but must consume at least 1 step
                            if (R == 0) {
                                remaining_time_after_failure = std::max(n - 1, 0L);
                            }
                            else {
                                remaining_time_after_failure = std::max(n - R, 0L);
                            }
                        }
                        else {
                            remaining_time_after_failure = std::max(n - u - R, 0L);
                        }
                    }
                    else {
                        remaining_time_after_failure = std::max(n - u - R - 1, 0L);
                    }

                    double retry_task_error = calculate_expected_error(
                        remaining_tasks,
                        task_index,
                        running_input_data_size,
                        running_input_error_level,
                        selected_delta_t,
                        dp,
                        current_task_node,
                        remaining_time_after_failure,
                        R,
                        deadline,
                        lower_bound
                    );
                    expected_error += _probability_computation->fail_probability(u) * retry_task_error;
                }
            }

            if (expected_error < min_expected_error) {
                min_expected_error = expected_error;
                best_option = option_name;
            }
        }

        // std::cout << "Best option: " << best_option << " at time "<< n << " for task number " << task_index << std::endl;
        dp[current_task_node][n] = std::make_pair(best_option, min_expected_error);
        return min_expected_error;
    }


    /**
     * DYNAMIC SCHEDULING ALGORITHM
     *
     * @brief Selects the best execution option based on the lowest E(x, y, n)
     * @param hostname The name of the host duh
     * @param input_data_size This is our x
     * @param input_error_level This is our y
     * @param remaining_time This is our n, which is the remaining time until the deadline
     * @param lower_bound Whether the lower-bound should be computed (instead of the upper bound)
     * @return The name of the best execution option
     */
    void SchedulingAlgorithmDynamic::fill_host_preprocessing_table(const std::string& hostname,
                                                                   const double input_data_size,
                                                                   const double input_error_level,
                                                                   const double remaining_time,
                                                                   const bool lower_bound) {
        const long n = lower_bound ?
            ceiling_division(remaining_time, _delta_t) :
            floor_division(remaining_time, _delta_t);
        const long R = lower_bound ?
            floor_division(_application_specs->get_restart_overhead(), _delta_t) :
            ceiling_division(_application_specs->get_restart_overhead(), _delta_t);

        std::map<const ApplicationSpecs::ExecOptionDecisionNode*, std::vector<std::pair<std::string, double>>> dp;

        // FIXME: Not a big fan of this brute forcing but whatever
        const int num_tasks = static_cast<int>(_exec_options.size()) - 1;
        const int task_to_schedule_index = _application_specs->get_host_task_to_schedule_index(hostname);
        const ApplicationSpecs::ExecOptionDecisionNode* current_decision_node = _application_specs->
            get_host_current_decision_node(hostname);
        for (long i = 0; i <= n; i++) {
            // std::cout << "ITERATION i = " << i << std::endl;
            calculate_expected_error(num_tasks - task_to_schedule_index, task_to_schedule_index, input_data_size,
                                     input_error_level, _delta_t, dp,
                                     current_decision_node, i, R, n, lower_bound);
        }

        // for (auto &entry : dp) {
        //     for (int j = 0; j < dp[entry.first].size(); j++) {
        //         std::cerr << "dp[" << entry.first->task << ", " << entry.first->execution_option <<"][" << j << "] = (" << dp[entry.first][j].first << ", " << dp[entry.first][j].second << ")" << std::endl;
        //     }
        // }

        _optimal_EV = dp[_application_specs->get_decision_tree_root(hostname)][n].second;

        for (auto& entry : dp) {
            std::vector<std::pair<long, std::string>> compressed;
            std::string last_option = "";

            for (int j = 0; j < dp[entry.first].size(); j++) {
                const auto& [option, error] = dp[entry.first][j];

                if (option == "impossible" || option.empty()) continue;

                if (option != last_option) {
                    // Only check if option changes
                    compressed.push_back({j, option});
                    last_option = option;
                }
            }
            _preprocessed_decisions_by_host[hostname].emplace(entry.first, std::move(compressed));
        }

        // for (auto &entry : _preprocessed_decisions) {
        //     std::cerr << "Preprocessed decisions for node (task: " << entry.first->task
        //               << ", execution_option: " << entry.first->execution_option << "):" << std::endl;
        //     for (const auto& [time, option] : entry.second) {
        //         std::cerr << "  Time >= " << time << ": Option = " << option << std::endl;
        //     }
        // }
    }


    // FIXME: This needs to be moved to another file anyway
    // double SchedulingAlgorithmDynamic::compute_best_delta_t(const double initial_data_size,
    //     const double initial_error_level,
    //     const double deadline,
    //     double precision) {
    //
    //     // Remember the current delta to restore it later (pretty hacky)
    //     double current_delta_t = _delta_t;
    //
    //     double lo = 1.0; // What to put here?
    //     double hi = 10000.0; // What to put here?
    //     double best_deltat = lo;
    //
    //     while (std::abs(hi - lo) / hi > precision) {
    //         std::cerr << "HI=" << hi << "  LO=" <<  lo << std::endl;
    //         double mid = (lo + hi) / 2;
    //
    //         _probability_computation->set_delta_t(mid);
    //         _delta_t = mid;
    //
    //         // Lower bound
    //         _preprocessed_decisions.clear();
    //         this->fill_preprocessing_table(initial_data_size, initial_error_level, deadline, true);
    //         double result_lower_bound = this->get_expected_error();
    //
    //         // Upper bound
    //         _preprocessed_decisions.clear();
    //         this->fill_preprocessing_table(initial_data_size, initial_error_level, deadline, false);
    //         double result_upper_bound = this->get_expected_error();
    //
    //         double result_avg = (result_upper_bound + result_lower_bound) / 2;
    //         std::cerr << "EV(MID) UPPER BOUND = " << result_upper_bound <<
    //             "   EV(MID) LOWER BOUND = " << result_lower_bound <<
    //             "   EV(MID) AVG = " << result_avg << std::endl;
    //
    //         if ((std::abs(result_upper_bound - result_lower_bound) / result_upper_bound) < precision) {
    //             // Precision is good enough — try a larger deltat
    //             std::cerr << "Precision is good enough - trying a larger deltat    Current deltat " << mid << std::endl << std::endl;
    //             best_deltat = mid;
    //             lo = mid;
    //         } else {
    //             // Not precise enough — reduce deltat
    //             std::cerr << "Not precise enough - reducing deltat    Current deltat = " << mid << std::endl << std::endl;
    //             hi = mid;
    //         }
    //     }
    //
    //     // Restore original delta_t
    //     _delta_t = current_delta_t;
    //     _probability_computation->set_delta_t(current_delta_t);
    //
    //     return best_deltat;
    // }

    std::vector<SchedulingAlgorithm::SchedulingDecision> SchedulingAlgorithmDynamic::make_decisions(
        SystemState* system_state_tracker,
        const double remaining_time) {
        // Make a decision for each host that's currently idle
        // All these decisions are independent so that makes it easy
        std::vector<SchedulingDecision> decisions;
        for (const auto& entry : *system_state_tracker) {
            std::string hostname = entry.first;
            std::string task_to_schedule = _application_specs->get_host_task_to_schedule(hostname);

            if (_preprocessed_decisions_by_host.find(hostname) == _preprocessed_decisions_by_host.end()) {
                // std::cerr << "preprocessing for host " << hostname << " not found" << std::endl;
                // std::function<void(const ApplicationSpecs::ExecOptionDecisionNode*, int)> print_tree = [&](const ApplicationSpecs::ExecOptionDecisionNode* node, int depth) {
                //     if (!node) return;
                //
                //     std::string indent(depth * 2, ' ');
                //
                //     std::cout << indent
                //               << "execution_option: " << node->execution_option
                //               << " | task: " << node->task
                //               << " | error_lvl: " << node->cumulative_error_factor
                //               << std::endl;
                //
                //     for (const auto& child : node->children) {
                //         print_tree(child.get(), depth + 1);
                //     }
                // };
                // print_tree(_application_specs->get_decision_tree_root(hostname), 0);
                double host_running_data_size = _application_specs->get_host_running_data_size(hostname);
                double host_running_error_level = _application_specs->get_host_running_error_level(hostname);
                preprocess_host_decisions(hostname, host_running_data_size, host_running_error_level, remaining_time,
                                          true);
            }

            if (system_state_tracker->is_host_idle(hostname)) {
                const ApplicationSpecs::ExecOptionDecisionNode* current_decision_node = _application_specs->
                    get_host_current_decision_node(hostname);
                const auto n = static_cast<long>(std::floor(remaining_time / _delta_t));

                const auto& curr_node_decisions = _preprocessed_decisions_by_host.at(hostname).
                    at(current_decision_node);


                auto it = std::upper_bound(curr_node_decisions.begin(), curr_node_decisions.end(), n,
                                           [](long time, const std::pair<long, std::string>& entry) {
                                               return time < entry.first;
                                           });
                --it;
                const std::string& execution_option = it->second;

                // std::cout << "Selected execution_option " << execution_option << " after task completion " << current_decision_node->task <<
                //     " on host " << hostname << " with remaining time " << remaining_time << std::endl;
                decisions.push_back({hostname, task_to_schedule, execution_option});
            }
        }
        return decisions;
    }
}
