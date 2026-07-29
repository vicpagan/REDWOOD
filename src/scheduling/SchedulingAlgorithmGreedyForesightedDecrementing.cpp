#include <iostream>
#include <limits>
#include <cmath>

#include "scheduling/SchedulingAlgorithmGreedyForesightedDecrementing.h"
#include "ApplicationSpecs.h"
#include "ProbabilityComputation.h"
#include "Utils.h"

namespace wrench {

    void SchedulingAlgorithmGreedyForesightedDecrementing::initial_decisions(const std::string& hostname, double initial_data_size, double initial_error_level, double deadline, bool lower_bound) {

        if (_delta_t_scheme == "fixed") {
            _delta_t = _delta_t_parameter;
        }
        else if (_delta_t_scheme == "compute") {
            throw std::invalid_argument("Static foresighted does not support 'compute' delta_t_scheme");
        }
        else {
            throw std::invalid_argument("Unknown delta_t_scheme '" + _delta_t_scheme + "'");
        }
        _probability_computation->set_delta_t(_delta_t);

        const long d = lower_bound ?
            ceiling_division(deadline, _delta_t) :
            floor_division(deadline, _delta_t);
        const long R = lower_bound ?
            floor_division(_application_specs->get_restart_overhead(), _delta_t) :
            ceiling_division(_application_specs->get_restart_overhead(), _delta_t);

        const int num_tasks = static_cast<int>(_exec_options.size()) - 1;
        const ApplicationSpecs::ExecOptionDecisionNode *current_decision_node = _application_specs->get_host_current_decision_node(hostname);

        // initialize list of combinations
        std::vector<std::vector<std::string>> all_combinations;
        std::vector<std::string> current_path;
        collect_combinations(all_combinations, current_decision_node, current_path);

        // sort list of combinations by error level
        std::map<std::vector<std::string>, double> el_by_combo;
        for (const auto &combo : all_combinations) {
            el_by_combo.emplace(combo, calculate_error_level_one_host(current_decision_node, combo, 0));
        }
        std::sort(all_combinations.begin(), all_combinations.end(), [&](const auto &a, const auto &b) {
            return el_by_combo.at(a) < el_by_combo.at(b);
        });

        // initialize the decisions starting at N nodes per combination
        for (const auto &combo : all_combinations) {
            _nodes_per_combo_decision[combo] = _num_compute_nodes;
        }
        long num_nodes_scheduled = _num_compute_nodes * static_cast<long>(all_combinations.size());

        // Select combo with the highest probability of success
        // Schedules this combo on all compute nodes
        if (dynamic_cast<ProbabilitySuccessComparator*>(_comparator_function)) {
            double best_ps = -std::numeric_limits<double>::infinity();
            std::vector<std::string> best_combo;

            std::map<const ApplicationSpecs::ExecOptionDecisionNode*, std::vector<double>> dp;
            for (const auto &combo : all_combinations) {

                double ps = calculate_prob_success_one_host(
                    num_tasks, 0, initial_data_size, initial_error_level,
                    _delta_t, dp, current_decision_node,
                    combo, 0,
                    d, R, d, lower_bound);
                dp.clear();

                if (ps > best_ps) {
                    best_ps = ps;
                    best_combo = combo;
                }
            }

            for (const auto &combo : all_combinations) {
                if (combo != best_combo) {
                    _nodes_per_combo_decision[combo] = 0;
                }
            }
        }
        // Schedules based on the lowest expected error of any combination of option paths (combination of combinations lol)
        else if (dynamic_cast<ExpectedErrorComparator*>(_comparator_function)) {
            std::map<std::vector<std::string>, double> ps_by_combo;

            std::map<const ApplicationSpecs::ExecOptionDecisionNode*, std::vector<double>> dp;
            for (const auto &combo : all_combinations) {

                ps_by_combo[combo] = calculate_prob_success_one_host(
                    num_tasks, 0, initial_data_size, initial_error_level,
                    _delta_t, dp, current_decision_node,
                    combo, 0,
                    d, R, d, lower_bound);
                dp.clear();
            }

            while (num_nodes_scheduled > _num_compute_nodes) {
                double best_exp_err = std::numeric_limits<double>::infinity();
                std::vector<std::string> combo_to_remove;

                for (const auto &combo : all_combinations) {
                    _nodes_per_combo_decision[combo]--;
                    if (_nodes_per_combo_decision[combo] >= 0) {
                        double exp_err = this->calculate_expected_error(all_combinations, ps_by_combo, el_by_combo);
                        if (exp_err < best_exp_err) {
                            best_exp_err = exp_err;
                            combo_to_remove = combo;
                        }
                    }
                    _nodes_per_combo_decision[combo]++;
                }
                _nodes_per_combo_decision[combo_to_remove]--;
                num_nodes_scheduled--;
            }
        }
        //
        else if (dynamic_cast<SuccessErrorRatioComparator*>(_comparator_function)) {
            double best_ps_el_ratio = -std::numeric_limits<double>::infinity();
            std::vector<std::string> best_combo;

            std::map<const ApplicationSpecs::ExecOptionDecisionNode*, std::vector<double>> dp;
            for (const auto &combo : all_combinations) {

                double ps = calculate_prob_success_one_host(
                    num_tasks, 0, initial_data_size, initial_error_level,
                    _delta_t, dp, current_decision_node,
                    combo, 0,
                    d, R, d, lower_bound);
                dp.clear();
                double el = el_by_combo[combo];

                double ps_el_ratio = ps / el;

                if (ps_el_ratio > best_ps_el_ratio) {
                    best_ps_el_ratio = ps_el_ratio;
                    best_combo = combo;
                }
            }

            for (const auto &combo : all_combinations) {
                if (combo != best_combo) {
                    _nodes_per_combo_decision[combo] = 0;
                }
            }
        }
        else if (auto error_level_comparator_function = dynamic_cast<ErrorLevelComparator*>(_comparator_function)) {
            double best_el = std::numeric_limits<double>::infinity();
            std::vector<std::string> best_combo;
            double ps_threshold = error_level_comparator_function->get_prob_success_threshold();

            std::map<const ApplicationSpecs::ExecOptionDecisionNode*, std::vector<double>> dp;
            for (const auto &combo : all_combinations) {

                double ps = calculate_prob_success_one_host(
                    num_tasks, 0, initial_data_size, initial_error_level,
                    _delta_t, dp, current_decision_node,
                    combo, 0,
                    d, R, d, lower_bound);
                dp.clear();
                double el = el_by_combo[combo];

                if (ps > ps_threshold) {
                    if (el < best_el) {
                        best_el = el;
                        best_combo = combo;
                    }
                }
            }

            for (const auto &combo : all_combinations) {
                if (combo != best_combo) {
                    _nodes_per_combo_decision[combo] = 0;
                }
            }
        }
        else {
            throw std::runtime_error("Unknown comparator type");
        }

    }

}
