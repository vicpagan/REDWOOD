#include <iostream>
#include <limits>
#include <cmath>

#include "scheduling/SchedulingAlgorithmGreedyNearsightedIncrementing.h"
#include "ApplicationSpecs.h"
#include "ProbabilityComputation.h"
#include "Utils.h"

namespace wrench {

    void SchedulingAlgorithmGreedyNearsightedIncrementing::initial_decisions(
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

#if OPTIMISTIC_EXECUTION
        const auto d = static_cast<long>(std::floor(deadline / _delta_t));
        const auto R = static_cast<long>(std::floor(_application_specs->get_restart_overhead() / _delta_t));
#else
        const auto d = ceiling_division(deadline, _delta_t);
        const auto R = ceiling_division(_application_specs->get_restart_overhead(), _delta_t);
#endif

        // initialize list of combinations
        std::string first_task = _application_specs->get_task(0);

        // sort list of combinations by error level
        std::map<std::string, double> el_by_option;
        for (const auto &option : _exec_options.at(first_task)) {
            _list_of_options.push_back(option.first);
            el_by_option.emplace(option.first, _exec_options.at(first_task).at(option.first).at("e_function")(1,1));
        }
        std::sort(_list_of_options.begin(), _list_of_options.end(), [&](const auto &a, const auto &b) {
            return el_by_option.at(a) < el_by_option.at(b);
        });

        // initialize the decisions starting at N nodes per combination
        for (const auto &option : _list_of_options) {
            _nodes_per_initial_option_decision[option] = 0;
        }
        long num_nodes_scheduled = 0;

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

            _nodes_per_initial_option_decision[best_option] = _num_compute_nodes;
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

            while (num_nodes_scheduled < _num_compute_nodes) {
                double best_exp_err = std::numeric_limits<double>::infinity();
                std::string option_to_add;

                for (const auto &option : _list_of_options) {
                    _nodes_per_initial_option_decision[option]++;
                    double exp_err = this->calculate_expected_error(ps_by_option, el_by_option);
                    if (exp_err < best_exp_err) {
                        best_exp_err = exp_err;
                        option_to_add = option;
                    }
                    _nodes_per_initial_option_decision[option]--;
                }
                _nodes_per_initial_option_decision[option_to_add]++;
                num_nodes_scheduled++;
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

            _nodes_per_initial_option_decision[best_option] = _num_compute_nodes;
        }
        else if (dynamic_cast<ErrorLevelComparator*>(_comparator_function)) {
            const std::string& option_to_keep = _list_of_options.front();
            _nodes_per_initial_option_decision[option_to_keep] = _num_compute_nodes;
        }
        else {
            throw std::runtime_error("Unknown comparator type");
        }
    }

}
