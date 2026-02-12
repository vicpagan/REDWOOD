#include "scheduling/SchedulingAlgorithmRandom.h"
#include <random>
#include <chrono>

namespace wrench {

    SchedulingAlgorithmRandom::SchedulingAlgorithmRandom(
        const std::shared_ptr<ApplicationSpecs>& application_specs,
        const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
        ProbabilityComputation* probability_computation)
        : SchedulingAlgorithm(application_specs, "random", exec_options, probability_computation),
          _rng(std::chrono::steady_clock::now().time_since_epoch().count()) {
    }

    double SchedulingAlgorithmRandom::get_optimal_expected_error() const {
        return _e_fail;
    }

    void SchedulingAlgorithmRandom::preprocess_decisions(
        const double input_data_size,
        const double input_error_level,
        const double remaining_time,
        const bool lower_bound) {
    }

    std::vector<SchedulingAlgorithm::SchedulingDecision> SchedulingAlgorithmRandom::make_decisions(
        SystemState* system_state_tracker,
        const std::string& task_to_schedule,
        const double remaining_time,
        const bool minimize) {

        std::vector<SchedulingDecision> decisions;

        // Get all execution options for this task
        const auto& task_options = _exec_options.at(task_to_schedule);

        // Create a vector of option names
        std::vector<std::string> option_names;
        for (const auto& [option_name, _] : task_options) {
            option_names.push_back(option_name);
        }

        // Randomly select an option for each idle host
        std::uniform_int_distribution<size_t> dist(0, option_names.size() - 1);

        for (const auto& entry : *system_state_tracker) {
            std::string hostname = entry.first;

            if (system_state_tracker->is_host_idle(hostname)) {
                std::string random_option = option_names[dist(_rng)];
                decisions.push_back({hostname, task_to_schedule, random_option});
            }
        }

        return decisions;
    }
}