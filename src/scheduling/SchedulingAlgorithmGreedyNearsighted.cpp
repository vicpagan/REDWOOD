#include "scheduling/SchedulingAlgorithmGreedyNearsighted.h"
#include "scheduling/HeuristicPlanning.h"
#include "ApplicationSpecs.h"
#include "Discretization.h"

namespace wrench {

    /**
     * @brief Initial decisions: decide how many hosts execute each option of the first task, sorted by error level,
     *        for the criterion (stored in _nodes_per_initial_option_decision)
     * @param hostname The host whose current decision node is used (the first host)
     * @param initial_data_size The application's initial data size
     * @param initial_error_level The application's initial error level
     * @param deadline The remaining time before the deadline
     * @param lower_bound Whether durations are discretized for the lower bound (instead of the upper bound)
     */
     void SchedulingAlgorithmGreedyNearsighted::initial_decisions(
        const std::string& hostname,
        const double initial_data_size,
        const double initial_error_level,
        const double deadline,
        const bool lower_bound) {

        _discretization->initialize_delta_t(_application_specs, _application_specs->get_decision_tree_root(hostname));

        // the options of the first task, sorted by error level
        std::vector<std::vector<std::string>> sequences;
        std::vector<double> ps, el;
        nearsighted_choices(*_application_specs, *_discretization,
                            _application_specs->get_host_current_decision_node(hostname), 0,
                            initial_data_size, initial_error_level, deadline, lower_bound, sequences, ps, el);
        sort_by_error_level(sequences, ps, el);

        // number of hosts that execute each option
        const auto allocation = allocate_hosts(_criterion, ps, el, _num_compute_nodes, _e_fail);
        for (size_t k = 0; k < sequences.size(); k++) {
            _nodes_per_initial_option_decision[sequences[k].front()] = allocation[k];
        }
    }

    /**
     * @brief Nearsighted planning of one host: choose the best option of its next task for the criterion,
     *        assuming that this task is the last one, and store it (a single entry, for that task)
     * @param hostname The host
     * @param input_data_size The input data size of the host's next task
     * @param input_error_level The input error level of the host's next task
     * @param remaining_time The remaining time before the deadline
     * @param lower_bound Whether durations are discretized for the lower bound (instead of the upper bound)
     */
    void SchedulingAlgorithmGreedyNearsighted::preprocess_host_decisions(
        const std::string& hostname,
        const double input_data_size,
        const double input_error_level,
        const double remaining_time,
        const bool lower_bound) {

        _discretization->initialize_delta_t(_application_specs, _application_specs->get_decision_tree_root(hostname));

        // the options of the host's next task
        const int task_to_schedule_index = _application_specs->get_host_task_to_schedule_index(hostname);
        std::vector<std::vector<std::string>> sequences;
        std::vector<double> ps, el;
        nearsighted_choices(*_application_specs, *_discretization,
                            _application_specs->get_host_current_decision_node(hostname), task_to_schedule_index,
                            input_data_size, input_error_level, remaining_time, lower_bound, sequences, ps, el);

        // one host: the best option for the criterion
        const size_t best = best_choice(_criterion, ps, el, _e_fail);
        _static_decisions_per_host[hostname][_application_specs->get_task(task_to_schedule_index)] = sequences[best].front();
    }

    /**
     * @brief Assign the options decided by initial_decisions() to the hosts, in the system state's order: each host
     *        gets a stored decision for the first task only
     * @param system_state_tracker The state of the hosts
     * @throws std::runtime_error if there are no hosts, or more decided hosts than hosts
     */
    void SchedulingAlgorithmGreedyNearsighted::translate_to_static_decisions(SystemState* system_state_tracker) {
        auto hostname_iterator = system_state_tracker->begin();
        if (hostname_iterator == system_state_tracker->end()) {
            throw std::runtime_error("translate_to_static_decisions: system_state_tracker has no hosts");
        }
        std::string current_hostname;

        for (const auto& [option, num_nodes] : _nodes_per_initial_option_decision) {
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
