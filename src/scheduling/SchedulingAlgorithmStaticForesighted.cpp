#include <iostream>

#include "scheduling/SchedulingAlgorithmStaticForesighted.h"
#include "scheduling/HeuristicPlanning.h"
#include "ApplicationSpecs.h"

namespace wrench {
    void SchedulingAlgorithmStaticForesighted::preprocess_host_decisions(
        const std::string &hostname,
        const double initial_data_size,
        const double initial_error_level,
        const double deadline,
        const bool lower_bound) {
        const int task_to_schedule_index = _application_specs->get_host_task_to_schedule_index(hostname);
        const auto candidates = foresighted_candidates(_application_specs, _probability_computation,
                                                       _application_specs->get_host_current_decision_node(hostname),
                                                       task_to_schedule_index, initial_data_size, initial_error_level,
                                                       deadline, lower_bound);
        // One PE: the best combination for the criterion
        const auto &best_combo = best_candidate(_criterion, candidates, _e_fail).options;

        int task_idx = task_to_schedule_index;
        for (const auto &option: best_combo) {
            _static_decisions_per_host[hostname][_application_specs->get_task(task_idx++)] = option;
        }
    }
}
