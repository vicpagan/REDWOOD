#include "scheduling/SchedulingAlgorithmRandom.h"
#include <random>
#include <chrono>

namespace wrench {

    /**
     * @brief Constructor (the random number generator is seeded with the current time)
     * @param application_specs The application specification
     * @param exec_options The option functions of each option of each task
     * @param discretization The discretization (unused)
     */
    SchedulingAlgorithmRandom::SchedulingAlgorithmRandom(
        const std::shared_ptr<ApplicationSpecs>& application_specs,
        const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
        Discretization* discretization)
        : SchedulingAlgorithm(application_specs, "random", exec_options, discretization, false),
          _rng(std::chrono::steady_clock::now().time_since_epoch().count()) {
    }

    /**
     * @brief Does nothing (decisions are made at random in make_decisions())
     * @param hostname The host
     * @param initial_data_size The input data size of the host's next task
     * @param initial_error_level The input error level of the host's next task
     * @param deadline The remaining time before the deadline
     * @param lower_bound Unused
     */
    void SchedulingAlgorithmRandom::preprocess_host_decisions(
        const std::string& hostname,
        const double initial_data_size,
        const double initial_error_level,
        const double deadline,
        const bool lower_bound) {
    }

    /**
     * @brief Choose an option of the next task uniformly at random for each idle host
     * @param system_state_tracker The state of the hosts
     * @param remaining_time The remaining time before the deadline (unused)
     * @return The decisions, one per idle host
     */
    std::vector<SchedulingAlgorithm::SchedulingDecision> SchedulingAlgorithmRandom::make_decisions(
        SystemState* system_state_tracker,
        const double remaining_time) {

        std::vector<SchedulingDecision> decisions;

        for (const auto& entry : *system_state_tracker) {
            std::string hostname = entry.first;
            std::string task_to_schedule = _application_specs->get_host_task_to_schedule(hostname);

            if (system_state_tracker->is_host_idle(hostname)) {
                // get all execution options for this task
                const auto current_decision_node = _application_specs->get_host_current_decision_node(hostname);

                // create a vector of option names
                std::vector<std::string> option_names;
                for (const auto& child : current_decision_node->children) {
                    option_names.push_back(child->execution_option);
                }

                // randomly select an option for each idle host
                std::uniform_int_distribution<size_t> dist(0, option_names.size() - 1);

                std::string random_option = option_names[dist(_rng)];
                decisions.push_back({hostname, task_to_schedule, random_option});
            }
        }

        return decisions;
    }
}
