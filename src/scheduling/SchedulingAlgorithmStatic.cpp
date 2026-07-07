#include <iostream>
#include <limits>
#include <cmath>

#include "scheduling/SchedulingAlgorithmStatic.h"
#include "ProbabilityComputation.h"
#include "SystemState.h"

namespace wrench {

    std::vector<SchedulingAlgorithm::SchedulingDecision> SchedulingAlgorithmStatic::make_decisions(
        SystemState* system_state_tracker,
        const double remaining_time) {

        std::vector<SchedulingDecision> decisions;
        for (const auto& entry : *system_state_tracker) {
            std::string hostname = entry.first;
            std::string task_to_schedule = _application_specs->get_host_task_to_schedule(hostname);

            if (_static_decisions_per_host.find(hostname) == _static_decisions_per_host.end() || _static_decisions_per_host.at(hostname).find(task_to_schedule) == _static_decisions_per_host.at(hostname).end()) {
                std::cerr << "Rescheduling for hostname " << hostname << " with remaining time " << remaining_time << std::endl;
                const double host_running_data_size = _application_specs->get_host_running_data_size(hostname);
                const double host_running_error_level = _application_specs->get_host_running_error_level(hostname);
                preprocess_host_decisions(hostname, host_running_data_size, host_running_error_level, remaining_time, true);
            }

            if (system_state_tracker->is_host_idle(hostname)) {
                std::string chosen_option = _static_decisions_per_host.at(hostname).at(task_to_schedule);

                std::cout << "Selected execution_option " << chosen_option << " for task " << task_to_schedule <<
                    " on host " << hostname << " with remaining time " << remaining_time << std::endl;
                decisions.push_back({hostname, task_to_schedule, chosen_option});
            }
        }
        return decisions;
    }
}