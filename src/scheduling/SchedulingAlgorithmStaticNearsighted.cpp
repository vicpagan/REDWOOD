#include "scheduling/SchedulingAlgorithmStaticNearsighted.h"
#include "scheduling/HeuristicPlanning.h"
#include "Discretization.h"

namespace wrench {

    /**
     * @brief Nearsighted planning of one host: choose the best option of its next task for the criterion,
     *        assuming that this task is the last one, and store it (a single entry, for that task)
     * @param hostname The host
     * @param input_data_size The input data size of the host's next task
     * @param input_error_level The input error level of the host's next task
     * @param remaining_time The remaining time before the deadline
     * @param lower_bound Whether durations are discretized for the lower bound (instead of the upper bound)
     */
    void SchedulingAlgorithmStaticNearsighted::preprocess_host_decisions(
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
}
