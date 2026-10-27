#include <iostream>

#include "scheduling/SchedulingAlgorithmGreedyForesighted.h"
#include "scheduling/HeuristicPlanning.h"
#include "ApplicationSpecs.h"
#include "ProbabilityComputation.h"
#include "Utils.h"

#include <wrench-dev.h>

WRENCH_LOG_CATEGORY(scheduling_greedy_foresighted, "Log category for SchedulingAlgorithmGreedyForesighted");

namespace wrench {
    void SchedulingAlgorithmGreedyForesighted::initial_decisions(
        const std::string &hostname,
        const double initial_data_size,
        const double initial_error_level,
        const double deadline,
        const bool lower_bound) {
        // All combinations of options, sorted by error level
        const auto candidates = foresighted_candidates(_application_specs, _probability_computation,
                                                       _application_specs->get_host_current_decision_node(hostname),
                                                       0, initial_data_size, initial_error_level, deadline,
                                                       lower_bound);

        // Number of PEs that execute each combination
        const auto allocation = allocate_pes(_criterion, candidates, _num_compute_nodes, _e_fail);
        for (size_t k = 0; k < candidates.size(); k++) {
            _nodes_per_combo_decision[candidates[k].options] = allocation[k];
        }
    }

    void SchedulingAlgorithmGreedyForesighted::preprocess_host_decisions(
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

    //FIXME: Does not properly handle when scheduling every host on a task that is NOT the first task
    void SchedulingAlgorithmGreedyForesighted::translate_to_static_decisions(SystemState *system_state_tracker) {

        auto hostname_iterator = system_state_tracker->begin();
        if (hostname_iterator == system_state_tracker->end()) {
            throw std::runtime_error("translate_to_static_decisions: system_state_tracker has no hosts");
        }
        std::string current_hostname;

        for (const auto &[combo, num_nodes]: _nodes_per_combo_decision) {
            for (int i = 0; i < num_nodes; i++) {
                if (hostname_iterator == system_state_tracker->end()) {
                    throw std::runtime_error(
                        "translate_to_static_decisions: ran out of hosts -- "
                        "total decided node count exceeds system_state_tracker's host count"
                    );
                }
                current_hostname = hostname_iterator->first;
                int task_idx = 0;
                for (const auto &option: combo) {
                    _static_decisions_per_host[current_hostname][_application_specs->get_task(task_idx)] = option;
                    task_idx++;
                }
                ++hostname_iterator;
            }
        }
    }
}
