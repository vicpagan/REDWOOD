#include "scheduling/SchedulingAlgorithmGreedy.h"
#include "Discretization.h"
#include "SystemState.h"

namespace wrench {

    /**
     * @brief If no decisions are stored at all, make the initial decisions for all hosts (initial_decisions() and
     *        translate_to_static_decisions()); otherwise plan each host that has no stored decision for its next
     *        task (preprocess_host_decisions()). Then return the stored option of each idle host.
     * @param system_state_tracker The state of the hosts
     * @param remaining_time The remaining time before the deadline
     * @return The decisions, one per idle host
     */
    std::vector<SchedulingAlgorithm::SchedulingDecision> SchedulingAlgorithmGreedy::make_decisions(
        SystemState* system_state_tracker,
        const double remaining_time) {

        std::vector<SchedulingDecision> decisions;
        for (const auto& entry : *system_state_tracker) {
            std::string hostname = entry.first;
            std::string task_to_schedule = _application_specs->get_host_task_to_schedule(hostname);

            // decision data structure is empty --> initialize
            if (_static_decisions_per_host.empty()) {
                double initial_data_size = _application_specs->get_initial_data_size();
                double initial_error_level = _application_specs->get_initial_error_level();
                initial_decisions(hostname, initial_data_size, initial_error_level, remaining_time, _lower_bound);
                translate_to_static_decisions(system_state_tracker);
            }
            // decision data structure is only empty for host --> schedule host independently
            else {
                auto host_it = _static_decisions_per_host.find(hostname);

                if (host_it == _static_decisions_per_host.end() || host_it->second.find(task_to_schedule) == host_it->second.end()) {
                    double host_running_data_size = _application_specs->get_host_running_data_size(hostname);
                    double host_running_error_level = _application_specs->get_host_running_error_level(hostname);
                    preprocess_host_decisions(hostname, host_running_data_size, host_running_error_level, remaining_time, _lower_bound);
                }
            }

            if (system_state_tracker->is_host_idle(hostname)) {
                std::string chosen_option = _static_decisions_per_host.at(hostname).at(task_to_schedule);
                decisions.push_back({hostname, task_to_schedule, chosen_option});
            }
        }
        return decisions;
    }
}