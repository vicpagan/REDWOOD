#include <limits>
#include <cmath>

#include "scheduling/SchedulingAlgorithmStatic.h"
#include "Discretization.h"
#include "SystemState.h"

namespace wrench {

    /**
     * @brief For each host, plan its decisions if none is stored for its next task (preprocess_host_decisions()),
     *        then, if the host is idle, return its stored option for that task
     * @param system_state_tracker The state of the hosts
     * @param remaining_time The remaining time before the deadline
     * @return The decisions, one per idle host
     */
    std::vector<SchedulingAlgorithm::SchedulingDecision> SchedulingAlgorithmStatic::make_decisions(
        SystemState* system_state_tracker,
        const double remaining_time) {

        std::vector<SchedulingDecision> decisions;
        for (const auto& entry : *system_state_tracker) {
            std::string hostname = entry.first;
            std::string task_to_schedule = _application_specs->get_host_task_to_schedule(hostname);

            if (_static_decisions_per_host.find(hostname) == _static_decisions_per_host.end() || _static_decisions_per_host.at(hostname).find(task_to_schedule) == _static_decisions_per_host.at(hostname).end()) {
                const double host_running_data_size = _application_specs->get_host_running_data_size(hostname);
                const double host_running_error_level = _application_specs->get_host_running_error_level(hostname);
                preprocess_host_decisions(hostname, host_running_data_size, host_running_error_level, remaining_time, _lower_bound);
            }

            if (system_state_tracker->is_host_idle(hostname)) {
                std::string chosen_option = _static_decisions_per_host.at(hostname).at(task_to_schedule);
                decisions.push_back({hostname, task_to_schedule, chosen_option});
            }
        }
        return decisions;
    }
}