#include <iostream>
#include <algorithm>

#include "scheduling/SchedulingAlgorithmGreedyNearsighted.h"
#include "scheduling/HeuristicPlanning.h"
#include "ApplicationSpecs.h"
#include "ProbabilityComputation.h"
#include "Utils.h"

namespace wrench {
    void SchedulingAlgorithmGreedyNearsighted::initial_decisions(
        const std::string &hostname,
        const double initial_data_size,
        const double initial_error_level,
        const double deadline,
        const bool lower_bound) {
        // The options of the first task, sorted by error level
        auto candidates = nearsighted_candidates(_application_specs, _probability_computation,
                                                 _application_specs->get_host_current_decision_node(hostname),
                                                 _application_specs->get_task(0), initial_data_size,
                                                 initial_error_level, deadline);
        std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
            return a.error_level < b.error_level;
        });

        // Number of PEs that execute each option
        const auto allocation = allocate_pes(_criterion, candidates, _num_compute_nodes, _e_fail);
        for (size_t k = 0; k < candidates.size(); k++) {
            _nodes_per_initial_option_decision[candidates[k].options.front()] = allocation[k];
        }
    }

    void SchedulingAlgorithmGreedyNearsighted::preprocess_host_decisions(
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

    void SchedulingAlgorithmGreedyNearsighted::translate_to_static_decisions(SystemState *system_state_tracker) {
        auto hostname_iterator = system_state_tracker->begin();
        if (hostname_iterator == system_state_tracker->end()) {
            throw std::runtime_error("translate_to_static_decisions: system_state_tracker has no hosts");
        }
        std::string current_hostname;

        for (const auto &[option, num_nodes]: _nodes_per_initial_option_decision) {
            for (int i = 0; i < num_nodes; i++) {
                if (hostname_iterator == system_state_tracker->end()) {
                    throw std::runtime_error(
                        "translate_to_static_decisions: ran out of hosts -- "
                        "total decided node count exceeds system_state_tracker's host count"
                    );
                }
                current_hostname = hostname_iterator->first;
                _static_decisions_per_host[current_hostname][_application_specs->get_task(0)] = option;
                ++hostname_iterator;
            }
        }
    }
}
