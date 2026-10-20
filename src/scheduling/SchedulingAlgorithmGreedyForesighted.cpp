#include <iostream>
#include <limits>
#include <cmath>

#include "scheduling/SchedulingAlgorithmGreedyForesighted.h"
#include "ApplicationSpecs.h"
#include "ProbabilityComputation.h"
#include "Utils.h"
#include "DynamicProgramming.h"

namespace wrench {
    void SchedulingAlgorithmGreedyForesighted::initial_decisions(
        const std::string &hostname,
        const double initial_data_size,
        const double initial_error_level,
        const double deadline,
        const bool lower_bound) {
        initialize_delta_t();

        const ApplicationSpecs::ExecOptionDecisionNode *current_decision_node = _application_specs->
                get_host_current_decision_node(hostname);

        // initialize list of combinations
        std::vector<std::vector<std::string> > all_combinations;
        std::vector<std::string> current_path;
        collect_combinations(all_combinations, current_decision_node, current_path);

        // sort list of combinations by error level
        std::map<std::vector<std::string>, double> el_by_combo;
        for (const auto &combo: all_combinations) {
            el_by_combo.emplace(combo, calculate_error_level_one_host(current_decision_node, combo, 0));
        }
        std::sort(all_combinations.begin(), all_combinations.end(), [&](const auto &a, const auto &b) {
            return el_by_combo.at(a) < el_by_combo.at(b);
        });

        // initialize the decisions starting at N nodes per combination
        for (const auto &combo: all_combinations) {
            _nodes_per_combo_decision[combo] = 0;
        }
        long num_nodes_scheduled = 0;

        // Select combo with the highest probability of success
        // Schedules this combo on all compute nodes
        if (dynamic_cast<ProbabilitySuccessComparator *>(_comparator_function)) {
            double best_ps = -std::numeric_limits<double>::infinity();
            std::vector<std::string> best_combo;

            for (const auto &combo: all_combinations) {
                // std::cout << "trying combo" << std::endl;

                double ps = compute_success_probability(*_application_specs, *_probability_computation, 0, combo,
                                                        initial_data_size, initial_error_level, deadline, lower_bound);
                // std::cout << "finished calculation for combo" << std::endl;

                if (ps > best_ps) {
                    best_ps = ps;
                    best_combo = combo;
                }
            }
            _nodes_per_combo_decision[best_combo] = _num_compute_nodes;
        }
        // Schedules based on the lowest expected error of any combination of option paths (combination of combinations lol)
        else if (dynamic_cast<ExpectedErrorComparator *>(_comparator_function)) {
            std::map<std::vector<std::string>, double> ps_by_combo;

            for (const auto &combo: all_combinations) {
                ps_by_combo[combo] = compute_success_probability(*_application_specs, *_probability_computation, 0,
                                                                 combo,
                                                                 initial_data_size, initial_error_level, deadline,
                                                                 lower_bound);
            }

            while (num_nodes_scheduled < _num_compute_nodes) {
                double best_exp_err = std::numeric_limits<double>::infinity();
                std::vector<std::string> combo_to_add;

                for (const auto &combo: all_combinations) {
                    _nodes_per_combo_decision[combo]++;
                    double exp_err = this->calculate_expected_error(all_combinations, ps_by_combo, el_by_combo);
                    if (exp_err < best_exp_err) {
                        best_exp_err = exp_err;
                        combo_to_add = combo;
                    }
                    _nodes_per_combo_decision[combo]--;
                }
                _nodes_per_combo_decision[combo_to_add]++;
                num_nodes_scheduled++;
            }
        }
        //
        else if (dynamic_cast<SuccessErrorRatioComparator *>(_comparator_function)) {
            double best_ps_el_ratio = -std::numeric_limits<double>::infinity();
            std::vector<std::string> best_combo;

            for (const auto &combo: all_combinations) {
                double ps = compute_success_probability(*_application_specs, *_probability_computation, 0, combo,
                                                        initial_data_size, initial_error_level, deadline, lower_bound);
                double el = el_by_combo[combo];

                double ps_el_ratio = ps / el;

                if (ps_el_ratio > best_ps_el_ratio) {
                    best_ps_el_ratio = ps_el_ratio;
                    best_combo = combo;
                }
            }
            _nodes_per_combo_decision[best_combo] = _num_compute_nodes;
        } else if (auto error_level_comparator_function = dynamic_cast<ErrorLevelComparator *>(_comparator_function)) {
            double best_el = std::numeric_limits<double>::infinity();
            std::vector<std::string> best_combo;
            double ps_threshold = error_level_comparator_function->get_prob_success_threshold();

            for (const auto &combo: all_combinations) {
                double ps = compute_success_probability(*_application_specs, *_probability_computation, 0, combo,
                                                        initial_data_size, initial_error_level, deadline, lower_bound);
                double el = el_by_combo[combo];

                if (ps > ps_threshold) {
                    if (el < best_el) {
                        best_el = el;
                        best_combo = combo;
                    }
                }
            }

            if (best_combo.empty()) {
                best_combo = all_combinations.front();
            }

            _nodes_per_combo_decision[best_combo] = _num_compute_nodes;
        } else {
            throw std::runtime_error("Unknown comparator type");
        }
    }

    void SchedulingAlgorithmGreedyForesighted::preprocess_host_decisions(
        const std::string &hostname,
        const double initial_data_size,
        const double initial_error_level,
        const double deadline,
        const bool lower_bound) {
        const int task_to_schedule_index = _application_specs->get_host_task_to_schedule_index(hostname);
        const auto best_combo = select_foresighted_combination(hostname, _comparator_function, initial_data_size,
                                                               initial_error_level, deadline, lower_bound);

        int task_idx = task_to_schedule_index;
        for (const auto &option: best_combo) {
            _static_decisions_per_host[hostname][_application_specs->get_task(task_idx++)] = option;
        }
    }

    double SchedulingAlgorithmGreedyForesighted::calculate_expected_error(
        const std::vector<std::vector<std::string> > &all_combinations,
        const std::map<std::vector<std::string>, double> &ps_by_combo,
        const std::map<std::vector<std::string>, double> &el_by_combo) const {
        double p_all_fail = 1.0;
        double weighted_sum = 0.0;
        double running_prod = 1.0;

        for (const auto &combo: all_combinations) {
            const double p_one_succeeds = 1.0 - std::pow(1.0 - ps_by_combo.at(combo),
                                                         _nodes_per_combo_decision.at(combo));

            weighted_sum += el_by_combo.at(combo) * p_one_succeeds * running_prod;
            running_prod *= (1.0 - p_one_succeeds);
            p_all_fail *= (1.0 - p_one_succeeds);
        }
        return weighted_sum + _e_fail * p_all_fail;
    }

    //FIXME: Does not properly handle when scheduling every host on a task that is NOT the first task
    void SchedulingAlgorithmGreedyForesighted::translate_to_static_decisions(SystemState *system_state_tracker) {
        // for (const auto& [combo, num_nodes] : _nodes_per_combo_decision) {
        //     std::cerr << "[DEBUG]   combo = [";
        //     for (const auto& option : combo) {
        //         std::cerr << option << ", ";
        //     }
        //     std::cerr << "]  num_nodes = " << num_nodes << std::endl;
        // }

        auto hostname_iterator = system_state_tracker->begin();
        if (hostname_iterator == system_state_tracker->end()) {
            throw std::runtime_error("translate_to_static_decisions: system_state_tracker has no hosts");
        }
        std::string current_hostname;

        for (const auto &[combo, num_nodes]: _nodes_per_combo_decision) {
            for (int i = 0; i < num_nodes; i++) {
                if (hostname_iterator == system_state_tracker->end()) {
                    throw std::runtime_error(
                        "translate_to_static_decisions: ran out of hosts -- "
                        "total decided node count exceeds system_state_tracker's host count"
                    );
                }
                current_hostname = hostname_iterator->first;
                int task_idx = 0;
                for (const auto &option: combo) {
                    _static_decisions_per_host[current_hostname][_application_specs->get_task(task_idx)] = option;
                    task_idx++;
                }
                ++hostname_iterator;
            }
        }
    }
}
