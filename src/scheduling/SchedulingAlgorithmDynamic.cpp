#include <iostream>
#include <cmath>

#include "scheduling/SchedulingAlgorithmDynamic.h"
#include "ProbabilityComputation.h"
#include "SystemState.h"
#include "Utils.h"
#include "DynamicProgramming.h"

namespace wrench {
    void SchedulingAlgorithmDynamic::preprocess_host_decisions(const std::string &hostname,
                                                               const double initial_data_size,
                                                               const double initial_error_level,
                                                               const double deadline,
                                                               const bool lower_bound) {
        initialize_delta_t();

        _preprocessed_decisions_by_host.erase(hostname);
        this->fill_host_preprocessing_table(hostname, initial_data_size, initial_error_level, deadline, lower_bound);
    }

    /**
     * DYNAMIC SCHEDULING ALGORITHM
     *
     * @brief Computes the optimal option for every decision node and number of remaining time steps,
     *        i.e., E(x, y, n), and stores it compressed: for each node, the time steps at which the
     *        optimal option changes
     * @param hostname The name of the host
     * @param input_data_size This is our x
     * @param input_error_level This is our y
     * @param remaining_time This is our n, which is the remaining time until the deadline
     * @param lower_bound Whether the lower-bound should be computed (instead of the upper bound)
     */
    void SchedulingAlgorithmDynamic::fill_host_preprocessing_table(const std::string &hostname,
                                                                   const double input_data_size,
                                                                   const double input_error_level,
                                                                   const double remaining_time,
                                                                   const bool lower_bound) {
        const int task_to_schedule_index = _application_specs->get_host_task_to_schedule_index(hostname);
        const ApplicationSpecs::ExecOptionDecisionNode *current_decision_node = _application_specs->
                get_host_current_decision_node(hostname);

        const auto dp = compute_expected_error_table(*_application_specs, *_probability_computation,
                                                     current_decision_node, task_to_schedule_index,
                                                     input_data_size, input_error_level, remaining_time, lower_bound);

        if (task_to_schedule_index == 0) {
            _optimal_EV = dp.at(current_decision_node).back().second;
        }

        for (const auto &[node, expected_errors]: dp) {
            std::vector<std::pair<long, std::string> > compressed;
            std::string last_option = "";

            for (int j = 0; j < static_cast<int>(expected_errors.size()); j++) {
                const auto &option = expected_errors[j].first;

                if (option.empty()) continue;

                if (option != last_option) {
                    // Only check if option changes
                    compressed.push_back({j, option});
                    last_option = option;
                }
            }
            _preprocessed_decisions_by_host[hostname].emplace(node, std::move(compressed));
        }
    }

    std::vector<SchedulingAlgorithm::SchedulingDecision> SchedulingAlgorithmDynamic::make_decisions(
        SystemState *system_state_tracker,
        const double remaining_time) {
        // Make a decision for each host that's currently idle
        // All these decisions are independent so that makes it easy
        std::vector<SchedulingDecision> decisions;
        for (const auto &entry: *system_state_tracker) {
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
                const ApplicationSpecs::ExecOptionDecisionNode *current_decision_node = _application_specs->
                        get_host_current_decision_node(hostname);
                const auto n = static_cast<long>(std::floor(remaining_time / _delta_t));

                const auto &curr_node_decisions = _preprocessed_decisions_by_host.at(hostname).
                        at(current_decision_node);


                auto it = std::upper_bound(curr_node_decisions.begin(), curr_node_decisions.end(), n,
                                           [](long time, const std::pair<long, std::string> &entry) {
                                               return time < entry.first;
                                           });
                --it;
                const std::string &execution_option = it->second;

                // std::cout << "Selected execution_option " << execution_option << " after task completion " << current_decision_node->task <<
                //     " on host " << hostname << " with remaining time " << remaining_time << std::endl;
                decisions.push_back({hostname, task_to_schedule, execution_option});
            }
        }
        return decisions;
    }
}
