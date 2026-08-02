#include <iostream>
#include <limits>
#include <cmath>

#include "scheduling/SchedulingAlgorithmGreedyNearsightedDecrementing.h"
#include "ApplicationSpecs.h"
#include "ProbabilityComputation.h"
#include "Utils.h"

namespace wrench {

    void SchedulingAlgorithmGreedyNearsightedDecrementing::initial_decisions(
        const std::string& hostname,
        const double initial_data_size,
        const double initial_error_level,
        const double deadline,
        const bool lower_bound) {

        if (_delta_t_scheme == "fixed") {
            _delta_t = _delta_t_parameter;
        }
        else if (_delta_t_scheme == "compute") {
            throw std::invalid_argument("Static nearsighted does not support 'compute' delta_t_scheme");
        }
        else {
            throw std::invalid_argument("Unknown delta_t_scheme '" + _delta_t_scheme + "'");
        }
        _probability_computation->set_delta_t(_delta_t);

        // const long d = lower_bound ?
        //     ceiling_division(deadline, _delta_t) :
        //     floor_division(deadline, _delta_t);
        // const long R = lower_bound ?
        //     floor_division(_application_specs->get_restart_overhead(), _delta_t) :
        //     ceiling_division(_application_specs->get_restart_overhead(), _delta_t);

        // initialize list of combinations
        std::string first_task = _application_specs->get_task(0);
        const auto current_decision_node = _application_specs->get_host_current_decision_node(hostname);

        // sort list of combinations by error level
        std::map<std::string, double> el_by_option;
        for (const auto &child : current_decision_node->children) {
            _list_of_options.push_back(child->execution_option);
            el_by_option.emplace(child->execution_option, _exec_options.at(first_task).at(child->execution_option).at("e_function")(1,1));
        }
        std::sort(_list_of_options.begin(), _list_of_options.end(), [&](const auto &a, const auto &b) {
            return el_by_option.at(a) < el_by_option.at(b);
        });

        // initialize the decisions starting at N nodes per combination
        for (const auto &option : _list_of_options) {
            _nodes_per_initial_option_decision[option] = _num_compute_nodes;
        }
        long num_nodes_scheduled = _num_compute_nodes * static_cast<long>(_list_of_options.size());

        // Select combo with the highest probability of success
        // Schedules this option on all compute nodes
        if (dynamic_cast<ProbabilitySuccessComparator*>(_comparator_function)) {
            double best_ps = -std::numeric_limits<double>::infinity();
            std::string best_option;

            for (const auto &option : _list_of_options) {

                double ps = _comparator_function->comp_value(_probability_computation,
                    _exec_options.at(first_task).at(option),
                    initial_data_size,
                    initial_error_level,
                    deadline);

                if (ps > best_ps) {
                    best_ps = ps;
                    best_option = option;
                }
            }

            for (const auto &option : _list_of_options) {
                if (option != best_option) {
                    _nodes_per_initial_option_decision[option] = 0;
                }
            }
        }
        // Schedules based on the lowest expected error of any combination of option paths (combination of combinations lol)
        else if (dynamic_cast<ExpectedErrorComparator*>(_comparator_function)) {
            std::map<std::string, double> ps_by_option;

            for (const auto &option : _list_of_options) {

                const double exec_option_time = _exec_options.at(first_task).at(option).at("t_function")(initial_data_size, initial_error_level);
                const double exec_option_data = _exec_options.at(first_task).at(option).at("d_function")(initial_data_size, initial_error_level);
                const double exec_option_time_total = (initial_data_size / _io_read_bandwidth_per_node) + exec_option_time +
                                                      (exec_option_data / _io_write_bandwidth_per_node);

                ps_by_option[option] = _probability_computation->compute_probability(exec_option_time_total, deadline, false);
            }

            while (num_nodes_scheduled > _num_compute_nodes) {
                double best_exp_err = std::numeric_limits<double>::infinity();
                std::string option_to_remove;

                for (const auto &option : _list_of_options) {
                    _nodes_per_initial_option_decision[option]--;
                    if (_nodes_per_initial_option_decision[option] >= 0) {
                        double exp_err = this->calculate_expected_error(ps_by_option, el_by_option);
                        if (exp_err < best_exp_err) {
                            best_exp_err = exp_err;
                            option_to_remove = option;
                        }
                    }
                    _nodes_per_initial_option_decision[option]++;
                }
                _nodes_per_initial_option_decision[option_to_remove]--;
                num_nodes_scheduled--;
            }
        }
        //
        else if (dynamic_cast<SuccessErrorRatioComparator*>(_comparator_function)) {
            double best_ps_el_ratio = -std::numeric_limits<double>::infinity();
            std::string best_option;

            for (const auto &option : _list_of_options) {

                double ps_el_ratio = _comparator_function->comp_value(_probability_computation,
                    _exec_options.at(first_task).at(option),
                    initial_data_size,
                    initial_error_level,
                    deadline);

                if (ps_el_ratio > best_ps_el_ratio) {
                    best_ps_el_ratio = ps_el_ratio;
                    best_option = option;
                }
            }

            for (const auto &option : _list_of_options) {
                if (option != best_option) {
                    _nodes_per_initial_option_decision[option] = 0;
                }
            }
        }
        else if (auto error_level_comparator_function = dynamic_cast<ErrorLevelComparator*>(_comparator_function)) {
            double best_el = std::numeric_limits<double>::infinity();
            std::string best_option;
            double ps_threshold = error_level_comparator_function->get_prob_success_threshold();

            std::map<const ApplicationSpecs::ExecOptionDecisionNode*, std::vector<double>> dp;
            for (const auto &option : _list_of_options) {

                double ps = _comparator_function->comp_value(_probability_computation,
                    _exec_options.at(first_task).at(option),
                    initial_data_size,
                    initial_error_level,
                    deadline);
                double el = el_by_option[option];

                if (ps > ps_threshold) {
                    if (el < best_el) {
                        best_el = el;
                        best_option = option;
                    }
                }
            }

            if (best_option.empty()) {
                best_option = _list_of_options.front();
            }

            for (const auto &option : _list_of_options) {
                if (option != best_option) {
                    _nodes_per_initial_option_decision[option] = 0;
                }
            }
        }
        else {
            throw std::runtime_error("Unknown comparator type");
        }
    }

}
