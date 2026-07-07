#include "scheduling/SchedulingAlgorithmRandom.h"
#include <random>
#include <chrono>
#include <iostream>

namespace wrench {

    SchedulingAlgorithmRandom::SchedulingAlgorithmRandom(
        const std::shared_ptr<ApplicationSpecs>& application_specs,
        const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
        ProbabilityComputation* probability_computation)
        : SchedulingAlgorithm(application_specs, "random", exec_options, probability_computation),
          _rng(std::chrono::steady_clock::now().time_since_epoch().count()) {
    }

    double SchedulingAlgorithmRandom::get_expected_error() const {
        return _e_fail;
    }

    void SchedulingAlgorithmRandom::preprocess_host_decisions(
        const std::string& hostname,
        const double initial_data_size,
        const double initial_error_level,
        const double deadline,
        const bool lower_bound) {
    }

    std::vector<SchedulingAlgorithm::SchedulingDecision> SchedulingAlgorithmRandom::make_decisions(
        SystemState* system_state_tracker,
        const double remaining_time) {

        std::vector<SchedulingDecision> decisions;

        for (const auto& entry : *system_state_tracker) {
            std::string hostname = entry.first;
            std::cerr << "HOSTNAME RANDOM MAKE DECISIONS = " << hostname << "\n";
            std::string task_to_schedule = _application_specs->get_host_task_to_schedule(hostname);

            if (system_state_tracker->is_host_idle(hostname)) {
                // Get all execution options for this task
                const auto current_decision_node = _application_specs->get_host_current_decision_node(hostname);

                // Create a vector of option names
                std::vector<std::string> option_names;
                std::cerr << "Options available for host " << hostname << ": ";
                for (const auto& child : current_decision_node->children) {
                    std::cerr << child->execution_option << "   ";
                    option_names.push_back(child->execution_option);
                }
                std::cerr << std::endl;

                // Randomly select an option for each idle host
                std::uniform_int_distribution<size_t> dist(0, option_names.size() - 1);

                std::string random_option = option_names[dist(_rng)];
                decisions.push_back({hostname, task_to_schedule, random_option});
            }
        }

        return decisions;
    }
}
