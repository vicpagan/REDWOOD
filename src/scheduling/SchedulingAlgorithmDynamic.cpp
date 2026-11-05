#include <algorithm>
#include <limits>
#include <cmath>

#include "scheduling/SchedulingAlgorithmDynamic.h"
#include "Discretization.h"
#include "RecursiveFormulae.h"
#include "SystemState.h"
#include "Utils.h"

namespace wrench {
    /**
     * @brief Select delta_t if not done yet, then (re)compute the host's table of best options
     *        (fill_host_preprocessing_table())
     * @param hostname The host
     * @param initial_data_size The input data size of the host's next task
     * @param initial_error_level The input error level of the host's next task
     * @param deadline The remaining time before the deadline
     * @param lower_bound Whether the table is computed for the lower bound (instead of the upper bound)
     */
    void SchedulingAlgorithmDynamic::preprocess_host_decisions(const std::string& hostname,
                                                               const double initial_data_size,
                                                               const double initial_error_level,
                                                               const double deadline,
                                                               const bool lower_bound) {
        // select a delta_t if not already done so
        _discretization->initialize_delta_t(_application_specs, _application_specs->get_decision_tree_root(hostname));

        _preprocessed_decisions_by_host.erase(hostname);
        this->fill_host_preprocessing_table(hostname, initial_data_size, initial_error_level, deadline, lower_bound);
    }

    /**
     * @brief Compute the best option, i.e., the one that minimizes E(x, y, n) (Eq. 1, in RecursiveFormulae), for every
     *        node below the host's current decision node and every number of remaining time steps n, and store it
     *        compressed: for each node, the numbers of time steps at which the best option changes
     * @param hostname The host
     * @param input_data_size This is our x, the input data size of the host's next task
     * @param input_error_level This is our y, the input error level of the host's next task
     * @param remaining_time The remaining time until the deadline (in seconds), which gives the largest n
     * @param lower_bound Whether the lower bound should be computed (instead of the upper bound)
     */
    void SchedulingAlgorithmDynamic::fill_host_preprocessing_table(const std::string& hostname,
                                                                   const double input_data_size,
                                                                   const double input_error_level,
                                                                   const double remaining_time,
                                                                   const bool lower_bound) {
        const int task_to_schedule_index = _application_specs->get_host_task_to_schedule_index(hostname);
        const ApplicationSpecs::ExecOptionDecisionNode* current_decision_node = _application_specs->get_host_current_decision_node(hostname);

        const auto dp = compute_expected_error_table(*_application_specs, *_discretization, current_decision_node,
                                                     task_to_schedule_index, input_data_size, input_error_level,
                                                     remaining_time, lower_bound);

        for (const auto& [node, expected_errors] : dp) {
            std::vector<std::pair<long, std::string>> compressed;
            std::string last_option = "";

            for (int j = 0; j < static_cast<int>(expected_errors.size()); j++) {
                const auto& option = expected_errors[j].first;

                if (option == "impossible" || option.empty()) continue;

                if (option != last_option) {
                    // only check if option changes
                    compressed.push_back({j, option});
                    last_option = option;
                }
            }
            _preprocessed_decisions_by_host[hostname].emplace(node, std::move(compressed));
        }
    }

    /**
     * @brief For each host, compute its table of best options if it has none, then, if the host is idle, look up the
     *        best option at its current decision node for n = floor(remaining_time / delta_t) remaining time steps
     * @param system_state_tracker The state of the hosts
     * @param remaining_time The remaining time before the deadline
     * @return The decisions, one per idle host
     */
    std::vector<SchedulingAlgorithm::SchedulingDecision> SchedulingAlgorithmDynamic::make_decisions(
        SystemState* system_state_tracker,
        const double remaining_time) {

        std::vector<SchedulingDecision> decisions;
        for (const auto& entry : *system_state_tracker) {
            std::string hostname = entry.first;
            std::string task_to_schedule = _application_specs->get_host_task_to_schedule(hostname);

            if (_preprocessed_decisions_by_host.find(hostname) == _preprocessed_decisions_by_host.end()) {
                double host_running_data_size = _application_specs->get_host_running_data_size(hostname);
                double host_running_error_level = _application_specs->get_host_running_error_level(hostname);
                preprocess_host_decisions(hostname, host_running_data_size, host_running_error_level, remaining_time,
                                          _lower_bound);
            }

            if (system_state_tracker->is_host_idle(hostname)) {
                const ApplicationSpecs::ExecOptionDecisionNode* current_decision_node = _application_specs->get_host_current_decision_node(hostname);
                const auto n = static_cast<long>(std::floor(remaining_time / _discretization->get_delta_t()));

                const auto& curr_node_decisions = _preprocessed_decisions_by_host.at(hostname).at(current_decision_node);


                auto it = std::upper_bound(curr_node_decisions.begin(), curr_node_decisions.end(), n,
                                           [](long time, const std::pair<long, std::string>& entry) {
                                               return time < entry.first;
                                           });
                --it;
                const std::string& execution_option = it->second;

                decisions.push_back({hostname, task_to_schedule, execution_option});
            }
        }
        return decisions;
    }
}
