#include "scheduling/SchedulingAlgorithmStaticNearsighted.h"

namespace wrench {
    void SchedulingAlgorithmStaticNearsighted::preprocess_host_decisions(
        const std::string &hostname,
        const double input_data_size,
        const double input_error_level,
        const double remaining_time,
        const bool lower_bound) {
        const std::string best_option = select_nearsighted_option(hostname, _comparator_function, input_data_size,
                                                                  input_error_level, remaining_time);
        _static_decisions_per_host[hostname][_application_specs->get_host_task_to_schedule(hostname)] = best_option;
    }
}
