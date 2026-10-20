#include <iostream>

#include "scheduling/SchedulingAlgorithmStaticForesighted.h"
#include "ApplicationSpecs.h"

namespace wrench {
    void SchedulingAlgorithmStaticForesighted::preprocess_host_decisions(
        const std::string &hostname,
        const double initial_data_size,
        const double initial_error_level,
        const double deadline,
        const bool lower_bound) {
        const int task_to_schedule_index = _application_specs->get_host_task_to_schedule_index(hostname);
        const auto best_combo = select_foresighted_combination(hostname, _comparator_function, initial_data_size,
                                                               initial_error_level, deadline, lower_bound);

        int task_idx = task_to_schedule_index;
        for (const auto &option: best_combo) {
            _static_decisions_per_host[hostname][_application_specs->get_task(task_idx++)] = option;
        }
    }
}
