#include "scheduling/SchedulingAlgorithmGreedyForesighted.h"
#include "scheduling/HeuristicPlanning.h"
#include "ApplicationSpecs.h"
#include "Discretization.h"

namespace wrench {

    /**
     * @brief Initial decisions: decide how many hosts execute each sequence of options for all tasks, for the
     *        criterion (stored in _nodes_per_combo_decision)
     * @param hostname The host whose current decision node is used (the first host)
     * @param initial_data_size The application's initial data size
     * @param initial_error_level The application's initial error level
     * @param deadline The remaining time before the deadline
     * @param lower_bound Whether durations are discretized for the lower bound (instead of the upper bound)
     */
    void SchedulingAlgorithmGreedyForesighted::initial_decisions(
        const std::string& hostname,
        const double initial_data_size,
        const double initial_error_level,
        const double deadline,
        const bool lower_bound) {

        _discretization->initialize_delta_t(_application_specs, _application_specs->get_decision_tree_root(hostname));

        // the sequences of options for all tasks, by increasing error level
        std::vector<std::vector<std::string>> sequences;
        std::vector<double> ps, el;
        foresighted_choices(*_application_specs, *_discretization,
                            _application_specs->get_host_current_decision_node(hostname), 0,
                            initial_data_size, initial_error_level, deadline, lower_bound, sequences, ps, el);

        // number of hosts that execute each sequence
        const auto allocation = allocate_hosts(_criterion, ps, el, _num_compute_nodes, _e_fail);
        for (size_t k = 0; k < sequences.size(); k++) {
            _nodes_per_combo_decision[sequences[k]] = allocation[k];
        }
    }

    /**
     * @brief Foresighted planning of one host: choose the best sequence of options for all of its remaining tasks
     *        for the criterion, and store it (one entry per remaining task)
     * @param hostname The host
     * @param initial_data_size The input data size of the host's next task
     * @param initial_error_level The input error level of the host's next task
     * @param deadline The remaining time before the deadline
     * @param lower_bound Whether durations are discretized for the lower bound (instead of the upper bound)
     */
    void SchedulingAlgorithmGreedyForesighted::preprocess_host_decisions(
        const std::string& hostname,
        const double initial_data_size,
        const double initial_error_level,
        const double deadline,
        const bool lower_bound) {

        _discretization->initialize_delta_t(_application_specs, _application_specs->get_decision_tree_root(hostname));

        // the sequences of options for the host's remaining tasks, by increasing error level
        const int task_to_schedule_index = _application_specs->get_host_task_to_schedule_index(hostname);
        std::vector<std::vector<std::string>> sequences;
        std::vector<double> ps, el;
        foresighted_choices(*_application_specs, *_discretization,
                            _application_specs->get_host_current_decision_node(hostname), task_to_schedule_index,
                            initial_data_size, initial_error_level, deadline, lower_bound, sequences, ps, el);

        // one host: the best sequence for the criterion
        const auto& best_combo = sequences[best_choice(_criterion, ps, el, _e_fail)];

        int task_idx = task_to_schedule_index;
        for (const auto &option : best_combo) {
            _static_decisions_per_host[hostname][_application_specs->get_task(task_idx++)] = option;
        }
    }

    // FIXME: Does not properly handle when scheduling every host on a task that is NOT the first task
    /**
     * @brief Assign the sequences decided by initial_decisions() to the hosts, in the system state's order: each
     *        host gets a stored decision for every task, from the first one
     * @param system_state_tracker The state of the hosts
     * @throws std::runtime_error if there are no hosts, or more decided hosts than hosts
     */
    void SchedulingAlgorithmGreedyForesighted::translate_to_static_decisions(SystemState* system_state_tracker) {

        auto hostname_iterator = system_state_tracker->begin();
        if (hostname_iterator == system_state_tracker->end()) {
            throw std::runtime_error("translate_to_static_decisions: system_state_tracker has no hosts");
        }
        std::string current_hostname;

        for (const auto& [combo, num_nodes] : _nodes_per_combo_decision) {
            for (int i = 0; i < num_nodes; i++) {
                if (hostname_iterator == system_state_tracker->end()) {
                    throw std::runtime_error(
                        "translate_to_static_decisions: ran out of hosts -- "
                        "total decided node count exceeds system_state_tracker's host count"
                    );
                }
                current_hostname = hostname_iterator->first;
                int task_idx = 0;
                for (const auto& option : combo) {
                    _static_decisions_per_host[current_hostname][_application_specs->get_task(task_idx)] = option;
                    task_idx++;
                }
                ++hostname_iterator;
            }
        }
    }

}
