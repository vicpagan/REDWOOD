#include <algorithm>
#include <iostream>
#include <limits>

#include "scheduling/SchedulingAlgorithm.h"
#include "ProbabilityComputation.h"
#include "scheduling/SchedulingAlgorithmDynamic.h"
#include "scheduling/SchedulingAlgorithmStaticForesighted.h"
#include "scheduling/SchedulingAlgorithmStaticNearsighted.h"
#include "scheduling/SchedulingAlgorithmGreedyForesighted.h"
#include "scheduling/SchedulingAlgorithmGreedyNearsighted.h"
#include "scheduling/SchedulingAlgorithmRandom.h"
#include "DynamicProgramming.h"


namespace wrench {
    /**
    * @brief Instantiate a scheduling algorithm given its type/name
    * @param type: the algorithm's type/name
    * @param application_specs
    */
    std::shared_ptr<SchedulingAlgorithm> SchedulingAlgorithm::create_scheduling_algorithm(const std::string &type,
        const std::shared_ptr<ApplicationSpecs> &application_specs,
        const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double
            (double, double)> > > > &exec_options,
        ProbabilityComputation *probability_computation) {
        if (type == "dynamic") {
            return std::make_shared<SchedulingAlgorithmDynamic>(application_specs, exec_options,
                                                                probability_computation);
        } else if (type.rfind("static_", 0) == 0) {
            // starts with "static_"
            std::string remainder = type.substr(7); // remove "static_"

            std::string variant;
            if (remainder.rfind("foresighted_", 0) == 0) {
                variant = "foresighted";
            } else if (remainder.rfind("nearsighted_", 0) == 0) {
                variant = "nearsighted";
            } else {
                throw std::invalid_argument(
                    "Invalid static algorithm format. Expected: static_<foresighted|nearsighted>_<comparator>");
            }

            // Create comparator
            std::string comparator_name = remainder.substr(12);
            OptionComparatorFunction *chosen_comparator = nullptr;

            if (comparator_name == "expected_error") {
                chosen_comparator = new ExpectedErrorComparator(application_specs);
            } else if (comparator_name == "probability_success") {
                chosen_comparator = new ProbabilitySuccessComparator(application_specs);
            } else if (comparator_name == "error_level") {
                chosen_comparator = new ErrorLevelComparator(application_specs);
            } else if (comparator_name == "success_error_ratio") {
                chosen_comparator = new SuccessErrorRatioComparator(application_specs);
            } else {
                throw std::invalid_argument("Unknown static comparator '" + comparator_name + "'");
            }

            // Create appropriate static variant
            if (variant == "foresighted") {
                return std::make_shared<SchedulingAlgorithmStaticForesighted>(
                    application_specs, exec_options, probability_computation, chosen_comparator);
            } else if (variant == "nearsighted") {
                return std::make_shared<SchedulingAlgorithmStaticNearsighted>(
                    application_specs, exec_options, probability_computation, chosen_comparator);
            } else {
                throw std::invalid_argument("Unknown static variant '" + variant + "'");
            }
        } else if (type.rfind("greedy_", 0) == 0) {
            // starts with "greedy_"
            std::string remainder_after_type = type.substr(7); // remove "greedy_"

            std::string variant;
            if (remainder_after_type.rfind("foresighted_", 0) == 0) {
                variant = "foresighted";
            } else if (remainder_after_type.rfind("nearsighted_", 0) == 0) {
                variant = "nearsighted";
            } else {
                throw std::invalid_argument(
                    "Invalid static algorithm format. Expected: static_<foresighted|nearsighted>_<comparator>");
            }

            std::string comparator_name = remainder_after_type.substr(12); // remove "foresighted_" or "nearsighted_"
            OptionComparatorFunction *chosen_comparator = nullptr;

            if (comparator_name == "expected_error") {
                chosen_comparator = new ExpectedErrorComparator(application_specs);
            } else if (comparator_name == "probability_success") {
                chosen_comparator = new ProbabilitySuccessComparator(application_specs);
            } else if (comparator_name == "error_level") {
                chosen_comparator = new ErrorLevelComparator(application_specs);
            } else if (comparator_name == "success_error_ratio") {
                chosen_comparator = new SuccessErrorRatioComparator(application_specs);
            } else {
                throw std::invalid_argument("Unknown static comparator '" + comparator_name + "'");
            }

            // Create appropriate greedy variant
            if (variant == "foresighted") {
                return std::make_shared<SchedulingAlgorithmGreedyForesighted>(
                    application_specs, exec_options, probability_computation, chosen_comparator);
            } else if (variant == "nearsighted") {
                return std::make_shared<SchedulingAlgorithmGreedyNearsighted>(
                    application_specs, exec_options, probability_computation, chosen_comparator);
            } else {
                throw std::invalid_argument("Unknown greedy variant '" + variant + "'");
            }
        } else if (type == "random") {
            return std::make_shared<
                SchedulingAlgorithmRandom>(application_specs, exec_options, probability_computation);
        } else {
            throw std::invalid_argument("Unknown scheduling algorithm '" + type + "'");
        }
    }

    /**
     * @brief Select delta_t according to the delta_t scheme (unless already set, e.g., with set_delta_t())
     *        and pass it on to the probability computation
     */
    void SchedulingAlgorithm::initialize_delta_t() {
        if (_delta_t == -1) {
            _delta_t = _application_specs->select_delta_t();
        }
        _probability_computation->set_delta_t(_delta_t);
    }

    /**
     * @brief Nearsighted planning: the option of the host's next task that is best according to the
     *        comparator, assuming that this task is the last task in the chain
     */
    std::string SchedulingAlgorithm::select_nearsighted_option(const std::string &hostname,
                                                               OptionComparatorFunction *comparator_function,
                                                               const double input_data_size,
                                                               const double input_error_level,
                                                               const double remaining_time) {
        initialize_delta_t();

        bool minimize = comparator_function->is_minimizing();

        std::string best_option;
        double best_value = minimize
                                ? std::numeric_limits<double>::infinity()
                                : -std::numeric_limits<double>::infinity();

        std::string task_to_schedule = _application_specs->get_host_task_to_schedule(hostname);
        const auto current_decision_node = _application_specs->get_host_current_decision_node(hostname);
        for (const auto &child: current_decision_node->children) {
            std::string option_name = child->execution_option;
            const auto &option_functions = _exec_options.at(task_to_schedule).at(option_name);
            double value = comparator_function->comp_value(
                _probability_computation,
                option_functions,
                input_data_size,
                input_error_level,
                remaining_time);

            bool better = minimize ? (value < best_value) : (value > best_value);
            if (better) {
                best_value = value;
                best_option = option_name;
            }
        }
        return best_option;
    }

    /**
     * @brief Foresighted planning: the combination of options for all of the host's remaining tasks that
     *        is best according to the comparator
     */
    std::vector<std::string> SchedulingAlgorithm::select_foresighted_combination(
        const std::string &hostname,
        OptionComparatorFunction *comparator_function,
        const double initial_data_size,
        const double initial_error_level,
        const double deadline,
        const bool lower_bound) {
        initialize_delta_t();

        const int task_to_schedule_index = _application_specs->get_host_task_to_schedule_index(hostname);
        const ApplicationSpecs::ExecOptionDecisionNode *current_decision_node = _application_specs->
                get_host_current_decision_node(hostname);

        std::cerr << "task_to_schedule_index = " << task_to_schedule_index << std::endl;
        std::cerr << "current_decision_node_task = " << current_decision_node->task << std::endl;
        std::cerr << "current_decision_node_option = " << current_decision_node->execution_option << std::endl;

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

        // probability of success of each combination
        std::map<std::vector<std::string>, double> ps_by_combo;
        for (const auto &combo: all_combinations) {
            ps_by_combo[combo] = compute_success_probability(*_application_specs, *_probability_computation,
                                                             task_to_schedule_index, combo, initial_data_size,
                                                             initial_error_level, deadline, lower_bound);
        }

        double best_value;
        std::vector<std::string> best_combo;
        // Select combo with the highest probability of success
        if (dynamic_cast<ProbabilitySuccessComparator *>(comparator_function)) {
            best_value = -std::numeric_limits<double>::infinity();
            for (const auto &combo: all_combinations) {
                double ps = ps_by_combo.at(combo);
                if (ps > best_value) {
                    best_value = ps;
                    best_combo = combo;
                }
            }
        }
        // Select combo with the lowest expected error
        else if (dynamic_cast<ExpectedErrorComparator *>(comparator_function)) {
            best_value = std::numeric_limits<double>::infinity();
            for (const auto &combo: all_combinations) {
                double ps = ps_by_combo.at(combo);
                double exp_err = el_by_combo.at(combo) * ps + _e_fail * (1.0 - ps);
                if (exp_err < best_value) {
                    best_value = exp_err;
                    best_combo = combo;
                }
            }
        }
        // Select combo with the highest probability of success / error level ratio
        else if (dynamic_cast<SuccessErrorRatioComparator *>(comparator_function)) {
            best_value = -std::numeric_limits<double>::infinity();
            for (const auto &combo: all_combinations) {
                double ps_el_ratio = ps_by_combo.at(combo) / el_by_combo.at(combo);
                if (ps_el_ratio > best_value) {
                    best_value = ps_el_ratio;
                    best_combo = combo;
                }
            }
        }
        // Select combo with the lowest error level, among those that can succeed
        else if (auto error_level_comparator_function = dynamic_cast<ErrorLevelComparator *>(comparator_function)) {
            best_value = std::numeric_limits<double>::infinity();
            double ps_threshold = error_level_comparator_function->get_prob_success_threshold();
            for (const auto &combo: all_combinations) {
                double el = el_by_combo.at(combo);
                if (ps_by_combo.at(combo) > ps_threshold) {
                    if (el < best_value) {
                        best_value = el;
                        best_combo = combo;
                    }
                }
            }
            // if theres no valid combo above the threshold, just choose the lowest error level
            if (best_combo.empty()) {
                best_combo = all_combinations.at(0);
            }
        } else {
            throw std::runtime_error("Unknown comparator type");
        }

        std::cout << "best_combo size = " << best_combo.size()
                << " for host " << hostname << std::endl;

        return best_combo;
    }

    void SchedulingAlgorithm::collect_combinations(
        std::vector<std::vector<std::string> > &all_combinations,
        const ApplicationSpecs::ExecOptionDecisionNode *node,
        std::vector<std::string> &current_path) {
        if (node->is_leaf) {
            all_combinations.push_back(current_path);
            return;
        }

        for (const auto &child: node->children) {
            current_path.push_back(child->execution_option);
            collect_combinations(all_combinations, child.get(), current_path);
            current_path.pop_back();
        }
    }

    double SchedulingAlgorithm::calculate_error_level_one_host(
        const ApplicationSpecs::ExecOptionDecisionNode *current_node,
        const std::vector<std::string> &combo,
        int relative_task_index) {
        if (current_node->is_leaf) {
            return current_node->cumulative_error_factor;
        }

        for (const auto &child: current_node->children) {
            if (child->execution_option == combo[relative_task_index]) {
                return calculate_error_level_one_host(child.get(), combo, relative_task_index + 1);
            }
        }

        throw std::runtime_error("No matching child found for option: " + combo[relative_task_index]);
    }
}
