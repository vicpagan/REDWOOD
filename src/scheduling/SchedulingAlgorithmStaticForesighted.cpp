#include "scheduling/SchedulingAlgorithmStaticForesighted.h"
#include "scheduling/HeuristicPlanning.h"
#include "Discretization.h"

namespace wrench {
    /**
     * @brief Foresighted planning of one host: choose the best sequence of options for all of its remaining tasks
     *        for the criterion, and store it (one entry per remaining task)
     * @param hostname The host
     * @param initial_data_size The input data size of the host's next task
     * @param initial_error_level The input error level of the host's next task
     * @param deadline The remaining time before the deadline
     * @param lower_bound Whether durations are discretized for the lower bound (instead of the upper bound)
     */
    void SchedulingAlgorithmStaticForesighted::preprocess_host_decisions(
        const std::string& hostname,
        const double initial_data_size,
        const double initial_error_level,
        const double deadline,
        const bool lower_bound) {

        _discretization->initialize_delta_t(_application_specs, _application_specs->get_decision_tree_root(hostname));

        // the sequences of options for the host's remaining tasks, by increasing error level
        const int task_to_schedule_index = _application_specs->get_host_task_to_schedule_index(hostname);
        const ApplicationSpecs::ExecOptionDecisionNode* current_decision_node = _application_specs->
            get_host_current_decision_node(hostname);

        std::vector<std::vector<std::string>> sequences;
        std::vector<double> ps, el;
        foresighted_choices(*_application_specs, *_discretization, current_decision_node, task_to_schedule_index,
                            initial_data_size, initial_error_level, deadline, lower_bound, sequences, ps, el);

        // one host: the best sequence for the criterion
        const auto& best_combo = sequences[best_choice(_criterion, ps, el, _e_fail)];

        int task_idx = task_to_schedule_index;
        for (const auto& option : best_combo) {
            _static_decisions_per_host[hostname][_application_specs->get_task(task_idx++)] = option;
        }
    }
}
