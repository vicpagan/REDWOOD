#include <iostream>
#include <limits>
#include <cmath>

#include "scheduling/SchedulingAlgorithmDynamic.h"
#include "ProbabilityComputation.h"
#include "SystemState.h"

namespace wrench {

    double SchedulingAlgorithmDynamic::get_optimal_expected_error() const {
        double optimal_EV = _preprocessed_decisions.at(_application_specs->get_decision_tree_root()).at(static_cast<size_t>(std::ceil(_application_specs->get_deadline() / _delta_t))).second;
        // std::cerr << "Optimal error: " << optimal_EV << std::endl;
        return optimal_EV;
    }

    void SchedulingAlgorithmDynamic::preprocess_decisions(ProbabilityComputation* probability_computation,
        const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
        const double initial_data_size,
        const double initial_error_level,
        const double deadline,
        const bool lower_bound) {

        // Select a delta_t if not already done so
        if (_delta_t == -1) {
            double selected_delta_t;
            if (_delta_t_scheme == "fixed") {
                selected_delta_t = _delta_t_parameter;
            } else if (_delta_t_scheme == "compute") {
                selected_delta_t = this->compute_best_delta_t(probability_computation, exec_options, initial_data_size, initial_error_level, deadline, 1e-3);
            } else {
                throw std::invalid_argument("Unknown delta_t_scheme '" + _delta_t_scheme + "'");
            }

            _delta_t = selected_delta_t;
            probability_computation->set_delta_t(selected_delta_t);
        }

        _preprocessed_decisions.clear();
        this->fill_preprocessing_table(probability_computation, exec_options, initial_data_size, initial_error_level, deadline, lower_bound);
    }

    double SchedulingAlgorithmDynamic::calculate_expected_error(
        int remaining_tasks,
        int task_index,
        double running_input_data_size,
        double running_input_error_level,
        double selected_delta_t,
        std::map<const ApplicationSpecs::ExecOptionDecisionNode*, std::vector<std::pair<std::string, double>>> &dp,
        ProbabilityComputation* probability_computation,
        const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>> &exec_option_metrics,
        const ApplicationSpecs::ExecOptionDecisionNode* current_task_node,
        const long n, const long R,
        const long deadline,
        const bool lower_bound) const {

        // Check if vector exists for this node
        if (dp.find(current_task_node) == dp.end()) {
            // Create it with the right size
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

        int num_options = current_task_node->num_children;
        for (int i = 0; i < num_options; i++) {
            auto current_child_node = current_task_node->children[i].get();
            const std::string option_name = current_child_node->execution_option;

            auto option_functions = exec_option_metrics.at(task_name).at(option_name);

            long exec_time = static_cast<long>(option_functions.at("t_function")(running_input_data_size, running_input_error_level) / selected_delta_t);
            double expected_error;

            if (n < exec_time) {
                expected_error = _application_specs->get_e_fail();
            } else {
                double updated_input_size = option_functions.at("d_function")(running_input_data_size, running_input_error_level);
                double updated_error_level = option_functions.at("e_function")(running_input_data_size, running_input_error_level);

                // Task success
                if (remaining_tasks == 0) {
                    expected_error = probability_computation->success_probability(exec_time) * updated_error_level;
                } else {
                    double next_task_error = calculate_expected_error(
                        remaining_tasks - 1,
                        task_index + 1,
                        updated_input_size,
                        updated_error_level,
                        selected_delta_t,
                        dp,
                        probability_computation,
                        exec_option_metrics,
                        current_child_node,
                        n - exec_time,
                        R,
                        deadline,
                        lower_bound
                    );

                    expected_error = probability_computation->success_probability(exec_time) * next_task_error;
                }

                // Task failure
                for (long u = 0; u < exec_time; u++) {

                    // FIXME: This is gross fuck branching statements
                    long remaining_time_after_failure;
                    if (lower_bound == 1) {
                        if (u == 0) {
                            // must consume at least 1 time step to avoid infinite loop
                            remaining_time_after_failure = std::max(n - R - 1, 0L);
                        } else {
                            remaining_time_after_failure = std::max(n - u - R, 0L);
                        }
                    } else {
                        remaining_time_after_failure = std::max(n - u - R - 1, 0L);
                    }

                    double retry_task_error = calculate_expected_error(
                        remaining_tasks,
                        task_index,
                        running_input_data_size,
                        running_input_error_level,
                        selected_delta_t,
                        dp,
                        probability_computation,
                        exec_option_metrics,
                        current_task_node,
                        remaining_time_after_failure,
                        R,
                        deadline,
                        lower_bound
                    );
                    expected_error += probability_computation->fail_probability(u) * retry_task_error;
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
     * @param probability_computation The probability computation utility
     * @param exec_options Map of execution options for the current task
     * @param input_data_size This is our x
     * @param input_error_level This is our y
     * @param remaining_time This is our n, which is the remaining time until the deadline
     * @return The name of the best execution option
     */
    void SchedulingAlgorithmDynamic::fill_preprocessing_table(
        ProbabilityComputation* probability_computation,
        const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
        const double input_data_size,
        const double input_error_level,
        const double remaining_time,
        const bool lower_bound) {

        const auto n = static_cast<long>(std::ceil(remaining_time / _delta_t));
        const auto R = static_cast<long>(std::ceil(_application_specs->get_restart_overhead() / _delta_t));

        std::map<const ApplicationSpecs::ExecOptionDecisionNode*, std::vector<std::pair<std::string, double>>> dp;

        // FIXME: Not a big fan of this brute forcing but whatever
        for (long i = n; i >= 0; i--) {
            calculate_expected_error(static_cast<int>(exec_options.size()) - 1, 0, input_data_size, input_error_level, _delta_t, dp,
                                 probability_computation, exec_options, _application_specs->get_decision_tree_root(), i, R, n, lower_bound);
        }

        // for (auto &entry : dp) {
        //     if (entry.first == _application_specs->get_decision_tree_root()) {
        //         for (int j = 0; j < dp[entry.first].size(); j++) {
        //             std::cout << "dp[" << entry.first->task << ", " << entry.first->execution_option <<"][" << j << "] = (" << dp[entry.first][j].first << ", " << dp[entry.first][j].second << ")" << std::endl;
        //         }
        //     }
        // }

        for (auto &entry : dp) {
            std::vector<std::pair<std::string,double>> exec_option_decisions(n+1, std::make_pair("", 0.0));
            for (int j = 0; j < dp[entry.first].size(); j++) {
                exec_option_decisions[j] = dp[entry.first][j];
            }
            _preprocessed_decisions.emplace(entry.first, std::move(exec_option_decisions));
        }
    }

    double SchedulingAlgorithmDynamic::compute_best_delta_t(ProbabilityComputation* probability_computation,
        const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
        const double initial_data_size,
        const double initial_error_level,
        const double deadline,
        double precision) {

        // Remember the current delta to restore it later (pretty hacky)
        double current_delta_t = _delta_t;

        double lo = 1.0; // What to put here?
        double hi = 10.0; // What to put here?
        double best_deltat = lo;

        while (std::abs(hi - lo) / hi > precision) {
            std::cerr << "HI=" << hi << "  LO=" <<  lo << std::endl;
            double mid = (lo + hi) / 2;

            probability_computation->set_delta_t(mid);
            _delta_t = mid;

            // Lower bound
            _preprocessed_decisions.clear();
            this->fill_preprocessing_table(probability_computation, exec_options, initial_data_size, initial_error_level, deadline, true);
            double result_lower_bound = this->get_optimal_expected_error();

            // Upper bound
            _preprocessed_decisions.clear();
            this->fill_preprocessing_table(probability_computation, exec_options, initial_data_size, initial_error_level, deadline, false);
            double result_upper_bound = this->get_optimal_expected_error();

            double result_avg = (result_upper_bound + result_lower_bound) / 2;
            std::cerr << "EV(MID) UPPER BOUND = " << result_upper_bound <<
                "   EV(MID) LOWER BOUND = " << result_lower_bound <<
                "   EV(MID) AVG = " << result_avg << std::endl;

            if ((std::abs(result_upper_bound - result_lower_bound) / result_upper_bound) < precision) {
                // Precision is good enough — try a larger deltat
                std::cerr << "Precision is good enough - trying a larger deltat    Current deltat " << mid << std::endl << std::endl;
                best_deltat = mid;
                lo = mid;
            } else {
                // Not precise enough — reduce deltat
                std::cerr << "Not precise enough - reducing deltat    Current deltat = " << mid << std::endl << std::endl;
                hi = mid;
            }
        }

        // Restore original delta_t
        _delta_t = current_delta_t;
        probability_computation->set_delta_t(current_delta_t);

        return best_deltat;
    }

    std::vector<SchedulingAlgorithm::SchedulingDecision> SchedulingAlgorithmDynamic::make_decisions(
        SystemState* system_state_tracker,
        ProbabilityComputation* probability_computation,
        const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
        const std::string& task_to_schedule,
        const double input_data_size,
        const double input_error_level,
        const double remaining_time,
        OptionComparatorFunction* comparator_function,
        const bool minimize) {
        std::vector<SchedulingDecision> decisions;

        // Make a decision for each host that's currently idle
        // All these decisions are independent so that makes it easy
        for (const auto& entry : *system_state_tracker) {
            std::string hostname = entry.first;

            if (!system_state_tracker->is_host_idle(hostname)) continue; // Host is not idle

            if (_preprocessed_decisions.empty()) {
                throw std::invalid_argument("Preprocessed decisions are not available");
            }

            const ApplicationSpecs::ExecOptionDecisionNode* current_decision_node;
            if (system_state_tracker->get_host_current_decision_node(hostname) == nullptr) {
                current_decision_node = _application_specs->get_decision_tree_root();
            } else {
                current_decision_node = system_state_tracker->get_host_current_decision_node(hostname);
            }

            const int n = static_cast<int>(std::floor(remaining_time / probability_computation->get_delta_t()));
            const std::string execution_option = _preprocessed_decisions.at(current_decision_node).at(n).first;
            std::cout << "Selected execution_option " << execution_option << " after task completion " << current_decision_node->task <<
                " on host " << hostname << " with remaining time " << remaining_time << std::endl;
            decisions.push_back({hostname, task_to_schedule, execution_option});
        }
        return decisions;
    }
}
