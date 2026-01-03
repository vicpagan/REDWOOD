#include <iostream>
#include <limits>
#include <cmath>

#include "scheduling/SchedulingAlgorithmDynamic.h"
#include "ProbabilityComputation.h"
#include "SystemState.h"

namespace wrench {

    double SchedulingAlgorithmDynamic::get_optimal_expected_error() const {
        // return _preprocessed_decisions.at(_application_specs->get_task(0)).at(static_cast<size_t>(std::ceil(_application_specs->get_deadline() / _delta_t))).second;
        return 0.0;
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
        const long deadline) const {

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
                        deadline
                    );

                    expected_error = probability_computation->success_probability(exec_time) * next_task_error;
                }

                // Task failure
                for (long u = 0; u < exec_time; u++) {
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
                        std::max(n - u - R - 1, 0L),
                        R,
                        deadline
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
    void SchedulingAlgorithmDynamic::preprocess_decisions(
        ProbabilityComputation* probability_computation,
        const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
        const double input_data_size,
        const double input_error_level,
        const double deadline) {

        _preprocessed_decisions.clear();

        /* Initialize selected_delta_t to +inf */
        double selected_delta_t = std::numeric_limits<double>::max();
        const double lambda = probability_computation->get_lambda();
        for (const auto& [option_name, option_functions] : exec_options) {
            // std::cerr << "LOOKING AT OPTION_NAME = " << option_name << std::endl;

            /* Select a delta based on the scheme */
            if (_delta_t_scheme == "fixed") {
                // _delta_t_parameter is our fixed delta_t value
                selected_delta_t = _delta_t_parameter;
            } else if (_delta_t_scheme == "compute") {
                // _delta_t_parameter is our precision for calculating a good enough delta_t
                // selected_delta_t = std::min(selected_delta_t, probability_computation->compute_best_deltat(
                // exec_option_metrics.at(option_name).at("t_function"), remaining_time, _delta_t_parameter));
                throw std::invalid_argument("Dynamic scheduling does not support 'compute' delta_t_scheme for multitask yet");
            } else {
                throw std::invalid_argument("Unknown delta_t_scheme '" + _delta_t_scheme + "'");
            }
        }
        probability_computation->set_delta_t(selected_delta_t);
        _delta_t = selected_delta_t;

        const auto d = static_cast<long>(std::ceil(deadline / selected_delta_t));
        const auto R = static_cast<long>(std::ceil(_application_specs->get_restart_overhead() / selected_delta_t));

        std::map<const ApplicationSpecs::ExecOptionDecisionNode*, std::vector<std::pair<std::string, double>>> dp;

        // FIXME: Not a big fan of this brute forcing but whatever
        for (long i = d; i >= 0; i--) {
            calculate_expected_error(static_cast<int>(exec_options.size()) - 1, 0, input_data_size, input_error_level, selected_delta_t, dp,
                                 probability_computation, exec_options, _application_specs->get_decision_tree_root(), i, R, d);
        }

        // for (auto &entry : dp) {
        //     for (int j = 0; j < dp[entry.first].size(); j++) {
        //         std::cout << "dp[" << entry.first->task << ", " << entry.first->execution_option <<"][" << j << "] = (" << dp[entry.first][j].first << ", " << dp[entry.first][j].second << ")" << std::endl;
        //     }
        // }

        for (auto &entry : dp) {
            std::vector<std::pair<std::string,double>> exec_option_decisions(d+1, std::make_pair("", 0.0));
            for (int j = 0; j < dp[entry.first].size(); j++) {
                exec_option_decisions[j] = dp[entry.first][j];
            }
            _preprocessed_decisions.emplace(entry.first, std::move(exec_option_decisions));
        }
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
