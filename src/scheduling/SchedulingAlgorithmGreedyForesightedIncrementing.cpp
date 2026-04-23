#include <iostream>
#include <limits>
#include <cmath>

#include "scheduling/SchedulingAlgorithmGreedyForesightedIncrementing.h"
#include "ApplicationSpecs.h"
#include "ProbabilityComputation.h"
#include "Utils.h"

namespace wrench {

    void SchedulingAlgorithmGreedyForesightedIncrementing::preprocess_decisions(
        const double initial_data_size,
        const double initial_error_level,
        const double deadline,
        const bool lower_bound) {

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

#if OPTIMISTIC_EXECUTION
        const auto d = static_cast<long>(std::floor(deadline / _delta_t));
        const auto R = static_cast<long>(std::floor(_application_specs->get_restart_overhead() / _delta_t));
#else
        const auto d = ceiling_division(deadline, _delta_t);
        const auto R = ceiling_division(_application_specs->get_restart_overhead(), _delta_t);
#endif

        // initialize list of combinations
        std::vector<std::string> current_path;
        collect_combinations(_application_specs->get_decision_tree_root(), current_path);

        // sort list of combinations by error level
        std::map<std::vector<std::string>, double> el_by_combo;
        for (const auto &combo : _all_combinations) {
            el_by_combo.emplace(combo, calculate_error_level_one_host(_application_specs->get_decision_tree_root(), combo, 0));
        }
        std::sort(_all_combinations.begin(), _all_combinations.end(), [&](const auto &a, const auto &b) {
            return el_by_combo.at(a) < el_by_combo.at(b);
        });

        // initialize the decisions starting at N nodes per combination
        for (const auto &combo : _all_combinations) {
            _nodes_per_combo_decision[combo] = 0;
        }
        long num_nodes_scheduled = 0;

        // Select combo with the highest probability of success
        // Schedules this combo on all compute nodes
        if (dynamic_cast<ProbabilitySuccessComparator*>(_comparator_function)) {
            double best_ps = -std::numeric_limits<double>::infinity();
            std::vector<std::string> best_combo;

            std::map<const ApplicationSpecs::ExecOptionDecisionNode*, std::vector<double>> dp;
            for (const auto &combo : _all_combinations) {
                // std::cout << "trying combo" << std::endl;
                const int num_tasks = static_cast<int>(combo.size()) - 1;

                double ps = calculate_prob_success_one_host(
                    num_tasks, 0, initial_data_size, initial_error_level,
                    _delta_t, dp, _application_specs->get_decision_tree_root(),
                    combo, d, R, d, lower_bound);
                dp.clear();
                // std::cout << "finished calculation for combo" << std::endl;

                if (ps > best_ps) {
                    best_ps = ps;
                    best_combo = combo;
                }
            }
            _nodes_per_combo_decision[best_combo] = _num_compute_nodes;
        }
        // Schedules based on the lowest expected error of any combination of option paths (combination of combinations lol)
        else if (dynamic_cast<ExpectedErrorComparator*>(_comparator_function)) {
            std::map<std::vector<std::string>, double> ps_by_combo;

            std::map<const ApplicationSpecs::ExecOptionDecisionNode*, std::vector<double>> dp;
            for (const auto &combo : _all_combinations) {
                const int num_tasks = static_cast<int>(combo.size()) - 1;

                ps_by_combo[combo] = calculate_prob_success_one_host(
                    num_tasks, 0, initial_data_size, initial_error_level,
                    _delta_t, dp, _application_specs->get_decision_tree_root(),
                    combo, d, R, d, lower_bound);
                dp.clear();
            }

            while (num_nodes_scheduled < _num_compute_nodes) {
                double best_exp_err = -std::numeric_limits<double>::infinity();
                std::vector<std::string> combo_to_add;

                for (const auto &combo : _all_combinations) {
                    _nodes_per_combo_decision[combo]++;
                    double exp_err = this->calculate_expected_error(ps_by_combo, el_by_combo);
                    if (exp_err > best_exp_err) {
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
        else if (dynamic_cast<SuccessErrorRatioComparator*>(_comparator_function)) {
            double best_ps_el_ratio = -std::numeric_limits<double>::infinity();
            std::vector<std::string> best_combo;

            std::map<const ApplicationSpecs::ExecOptionDecisionNode*, std::vector<double>> dp;
            for (const auto &combo : _all_combinations) {
                const int num_tasks = static_cast<int>(combo.size()) - 1;

                double ps = calculate_prob_success_one_host(
                    num_tasks, 0, initial_data_size, initial_error_level,
                    _delta_t, dp, _application_specs->get_decision_tree_root(),
                    combo, d, R, d, lower_bound);
                dp.clear();
                double el = el_by_combo[combo];

                double ps_el_ratio = ps / el;

                if (ps_el_ratio > best_ps_el_ratio) {
                    best_ps_el_ratio = ps_el_ratio;
                    best_combo = combo;
                }
            }
            _nodes_per_combo_decision[best_combo] = _num_compute_nodes;
        }
        else if (auto error_level_comparator_function = dynamic_cast<ErrorLevelComparator*>(_comparator_function)) {
            double best_el = -std::numeric_limits<double>::infinity();
            std::vector<std::string> best_combo;
            double ps_threshold = error_level_comparator_function->get_prob_success_threshold();

            std::map<const ApplicationSpecs::ExecOptionDecisionNode*, std::vector<double>> dp;
            for (const auto &combo : _all_combinations) {
                const int num_tasks = static_cast<int>(combo.size()) - 1;

                double ps = calculate_prob_success_one_host(
                    num_tasks, 0, initial_data_size, initial_error_level,
                    _delta_t, dp, _application_specs->get_decision_tree_root(),
                    combo, d, R, d, lower_bound);
                dp.clear();
                double el = el_by_combo[combo];

                if (ps > ps_threshold) {
                    if (el > best_el) {
                        best_el = el;
                        best_combo = combo;
                    }
                }
            }

            _nodes_per_combo_decision[best_combo] = _num_compute_nodes;
        }
        else {
            throw std::runtime_error("Unknown comparator type");
        }
    }

}
