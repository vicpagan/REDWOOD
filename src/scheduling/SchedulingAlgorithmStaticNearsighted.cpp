#include "scheduling/SchedulingAlgorithmStaticNearsighted.h"
#include "scheduling/HeuristicPlanning.h"

namespace wrench {
    void SchedulingAlgorithmStaticNearsighted::preprocess_host_decisions(
        const std::string &hostname,
        const double input_data_size,
        const double input_error_level,
        const double remaining_time,
        const bool lower_bound) {
        const std::string task_to_schedule = _application_specs->get_host_task_to_schedule(hostname);
        const auto candidates = nearsighted_candidates(_application_specs, _probability_computation,
                                                       _application_specs->get_host_current_decision_node(hostname),
                                                       task_to_schedule, input_data_size, input_error_level,
                                                       remaining_time);
        // One PE: the best option for the criterion
        _static_decisions_per_host[hostname][task_to_schedule] =
                candidates.empty() ? "" : best_candidate(_criterion, candidates, _e_fail).options.front();
    }
}
