#include <iostream>

#include "scheduling/SchedulingAlgorithmGreedy.h"
#include "ProbabilityComputation.h"
#include "SystemState.h"

namespace wrench {

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
                initial_decisions(hostname, initial_data_size, initial_error_level, remaining_time, true);

                translate_to_static_decisions(system_state_tracker);

                for (const auto& [hname, task] : _static_decisions_per_host) {
                    for (const auto& [taskname, option] : task) {
                        std::cout << "Task " << taskname << " has decision option " << option << " for hostname " << hname << std::endl;
                    }
                }
            }
            // decision data structure is only empty for host --> schedule host independently
            else {
                auto host_it = _static_decisions_per_host.find(hostname);

                if (host_it == _static_decisions_per_host.end() || host_it->second.find(task_to_schedule) == host_it->second.end()) {
                    double host_running_data_size = _application_specs->get_host_running_data_size(hostname);
                    double host_running_error_level = _application_specs->get_host_running_error_level(hostname);
                    preprocess_host_decisions(hostname, host_running_data_size, host_running_error_level, remaining_time, true);
                }
            }


            if (system_state_tracker->is_host_idle(hostname)) {
                std::cout << "Choosing option for task" << task_to_schedule << " for host " << hostname << "\n";

                std::string chosen_option = _static_decisions_per_host.at(hostname).at(task_to_schedule);

                std::cout << "Selected execution_option " << chosen_option << " for task " << task_to_schedule <<
                    " on host " << hostname << " with remaining time " << remaining_time << std::endl;
                decisions.push_back({hostname, task_to_schedule, chosen_option});
            }
        }
        return decisions;
    }
}