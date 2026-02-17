#include <iostream>
#include <limits>
#include <cmath>

#include "scheduling/SchedulingAlgorithmStatic.h"
#include "ProbabilityComputation.h"
#include "SystemState.h"

namespace wrench {

    double SchedulingAlgorithmStatic::get_optimal_expected_error() const {
        return _expected_error;
    }

    void SchedulingAlgorithmStatic::preprocess_decisions(const double initial_data_size,
        const double initial_error_level,
        const double deadline,
        const bool minimize) {

        // Set delta_t
        if (_delta_t_scheme == "fixed") {
            _delta_t = _delta_t_parameter;
        } else if (_delta_t_scheme == "compute") {
            throw std::invalid_argument("Static scheduling does not support 'compute' delta_t_scheme");
        } else {
            throw std::invalid_argument("Unknown delta_t_scheme '" + _delta_t_scheme + "'");
        }
        _probability_computation->set_delta_t(_delta_t);

        // Clear previous decisions
        _static_decisions.clear();

        // Simulate the execution to pick best options for each task
        double running_data_size = initial_data_size;
        double running_error_level = initial_error_level;
        double remaining_time = deadline;
        double cumulative_success_prob = 1.0;

        std::cerr << "SCHEDALGSTATIC: deadline = " << deadline << std::endl;


        // Iterate through each task in order
        for (size_t task_idx = 0; task_idx < _exec_options.size(); task_idx++) {
            std::string task_name = _application_specs->get_task(task_idx);

            double best_comp_value = minimize ?
                std::numeric_limits<double>::infinity() :
                -std::numeric_limits<double>::infinity();
            std::string best_option;

            // Evaluate all execution options for this task
            for (const auto& [option_name, option_functions] : _exec_options.at(task_name)) {
                std::cerr << "SCHEDALGSTATIC: remaining_time = " << remaining_time << std::endl;
                std::cerr << "option = " << option_name << std::endl;
                double comp_value = _comparator_function->comp_value(
                    _probability_computation,
                    option_functions,
                    running_data_size,
                    running_error_level,
                    remaining_time
                );

                if ((minimize && comp_value < best_comp_value) ||
                    (!minimize && comp_value > best_comp_value)) {
                    best_comp_value = comp_value;
                    best_option = option_name;
                }
            }

            // Store the best decision for this task
            _static_decisions[task_name] = best_option;

            // Update running state for next task
            auto& best_option_functions = _exec_options.at(task_name).at(best_option);
            double exec_time = best_option_functions.at("t_function")(running_data_size, running_error_level);
            std::cerr << "exec_time = " << exec_time << std::endl;
            double exec_time_total = (running_data_size / _io_read_bandwidth_per_node) +
                                    exec_time +
                                    (best_option_functions.at("d_function")(running_data_size, running_error_level) / _io_write_bandwidth_per_node);

            double task_success_prob = _probability_computation->compute_probability(
                exec_time_total, remaining_time, false);
            cumulative_success_prob *= task_success_prob;

            running_data_size = best_option_functions.at("d_function")(running_data_size, running_error_level);
            double next_error_level = best_option_functions.at("e_function")(running_data_size, running_error_level);

            // For the last task, compute expected error
            if (task_idx == _exec_options.size() - 1) {
                _expected_error = cumulative_success_prob * next_error_level +
                                 (1.0 - cumulative_success_prob) * _e_fail;
            }

            running_error_level = next_error_level;
            std::cerr << "subtracting exec_time_total = " << exec_time_total << " from remaining_time = " << remaining_time << std::endl;
            remaining_time -= exec_time_total;
        }
    }

    std::vector<SchedulingAlgorithm::SchedulingDecision> SchedulingAlgorithmStatic::make_decisions(
        SystemState* system_state_tracker,
        const std::string& task_to_schedule,
        const double remaining_time,
        const bool minimize) {

        std::vector<SchedulingDecision> decisions;

        // Get the precomputed decision for this task
        if (_static_decisions.find(task_to_schedule) == _static_decisions.end()) {
            throw std::runtime_error("No precomputed decision for task: " + task_to_schedule);
        }

        std::string chosen_option = _static_decisions.at(task_to_schedule);

        // Assign the same decision to all idle hosts
        for (const auto& entry : *system_state_tracker) {
            std::string hostname = entry.first;

            if (system_state_tracker->is_host_idle(hostname)) {
                decisions.push_back({hostname, task_to_schedule, chosen_option});
            }
        }

        return decisions;
    }
}