#include <iostream>
#include <limits>
#include <cmath>

#include "scheduling/SchedulingAlgorithmStatic.h"
#include "ProbabilityComputation.h"
#include "SystemState.h"

namespace wrench {

    std::vector<SchedulingAlgorithm::SchedulingDecision> SchedulingAlgorithmStatic::make_decisions(
        SystemState* system_state_tracker,
        const std::string& task_to_schedule,
        const double input_data_size,
        const double input_error_level,
        const double remaining_time) {

        if (_static_decisions.find(task_to_schedule) == _static_decisions.end()) {
            preprocess_decisions(input_data_size, input_error_level, remaining_time, true);
        }

        std::vector<SchedulingDecision> decisions;
        for (const auto& entry : *system_state_tracker) {
            std::string hostname = entry.first;

            if (!system_state_tracker->is_host_idle(hostname)) continue; // Host is not idle

            std::string chosen_option = _static_decisions.at(task_to_schedule);

            // std::cout << "Selected execution_option " << chosen_option << " for task " << task_to_schedule <<
            //     " on host " << hostname << " with remaining time " << remaining_time << std::endl;
            decisions.push_back({hostname, task_to_schedule, chosen_option});
        }
        return decisions;
    }
}