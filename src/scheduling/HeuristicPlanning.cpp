#include "scheduling/HeuristicPlanning.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

#include "RecursiveFormulae.h"

namespace wrench {

    /**
     * @brief Check that a criterion name is one of the four supported criteria
     * @param criterion The criterion name
     * @return true if the criterion is expected_error, probability_success, success_error_ratio or error_level
     */
    bool is_valid_criterion(const std::string &criterion) {
        return criterion == "expected_error" || criterion == "probability_success" ||
               criterion == "success_error_ratio" || criterion == "error_level";
    }

    /**
     * @brief Nearsighted choices: each option of the next task (the children of node, in decision tree order),
     *        as a sequence of one option, assuming that this task is the last one. The results are appended to
     *        sequences, ps and el.
     * @param application_specs The application specification
     * @param discretization The discretization (delta_t must be set)
     * @param node The host's current decision tree node
     * @param task_index The index of the next task
     * @param input_data_size The input data size of the next task
     * @param input_error_level The input error level of the next task
     * @param remaining_time The remaining time before the deadline
     * @param lower_bound Whether durations are discretized for the lower bound (instead of the upper bound)
     * @param sequences The options, each as a sequence of one option (output)
     * @param ps The probability of success of each option (output)
     * @param el The error level of each option's output, e_function(input_data_size, input_error_level) (output)
     */
    void nearsighted_choices(const ApplicationSpecs &application_specs,
                             const Discretization &discretization,
                             const ApplicationSpecs::ExecOptionDecisionNode *node,
                             const int task_index,
                             const double input_data_size,
                             const double input_error_level,
                             const double remaining_time,
                             const bool lower_bound,
                             std::vector<std::vector<std::string> > &sequences,
                             std::vector<double> &ps,
                             std::vector<double> &el) {
        const std::string task = application_specs.get_task(task_index);
        for (const auto &child: node->children) {
            const std::vector<std::string> sequence = {child->execution_option};
            const auto &option_functions = application_specs.get_task_functions().at(task).at(child->execution_option);
            sequences.push_back(sequence);
            ps.push_back(compute_success_probability(application_specs, discretization, task_index, sequence,
                                                     input_data_size, input_error_level, remaining_time,
                                                     lower_bound));
            el.push_back(option_functions.at("e_function")(input_data_size, input_error_level));
        }
    }

    /**
     * @brief Foresighted choices: all sequences of options for the remaining tasks below node, by increasing
     *        error level. The results are appended to sequences, ps and el.
     * @param application_specs The application specification
     * @param discretization The discretization (delta_t must be set)
     * @param node The host's current decision tree node
     * @param task_index The index of the next task (the first task of each sequence)
     * @param input_data_size The input data size of the next task
     * @param input_error_level The input error level of the next task
     * @param remaining_time The remaining time before the deadline
     * @param lower_bound Whether durations are discretized for the lower bound (instead of the upper bound)
     * @param sequences The sequences of options (output)
     * @param ps The probability that each sequence completes before the deadline (output)
     * @param el The error level of each sequence, from its decision tree leaf (output)
     */
    void foresighted_choices(const ApplicationSpecs &application_specs,
                             const Discretization &discretization,
                             const ApplicationSpecs::ExecOptionDecisionNode *node,
                             const int task_index,
                             const double input_data_size,
                             const double input_error_level,
                             const double remaining_time,
                             const bool lower_bound,
                             std::vector<std::vector<std::string> > &sequences,
                             std::vector<double> &ps,
                             std::vector<double> &el) {
        std::vector<std::string> current_path;
        collect_combinations(sequences, node, current_path);
        for (const auto &sequence: sequences) {
            el.push_back(calculate_error_level_one_host(node, sequence, 0));
        }
        ps.resize(sequences.size());
        sort_by_error_level(sequences, ps, el);

        for (size_t k = 0; k < sequences.size(); k++) {
            ps[k] = compute_success_probability(application_specs, discretization, task_index, sequences[k],
                                                input_data_size, input_error_level, remaining_time, lower_bound);
        }
    }

    /**
     * @brief Sort the choices by increasing error level (needed by expected_error_on_hosts()), keeping the three
     *        vectors aligned
     * @param sequences The sequences of options (sorted in place)
     * @param ps The probabilities of success (sorted in place)
     * @param el The error levels (sorted in place)
     */
    void sort_by_error_level(std::vector<std::vector<std::string> > &sequences,
                             std::vector<double> &ps,
                             std::vector<double> &el) {
        std::vector<size_t> order(sequences.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](const size_t a, const size_t b) {
            return el[a] < el[b];
        });

        std::vector<std::vector<std::string> > sorted_sequences;
        std::vector<double> sorted_ps;
        std::vector<double> sorted_el;
        for (const size_t k: order) {
            sorted_sequences.push_back(sequences[k]);
            sorted_ps.push_back(ps[k]);
            sorted_el.push_back(el[k]);
        }
        sequences = std::move(sorted_sequences);
        ps = std::move(sorted_ps);
        el = std::move(sorted_el);
    }

    /**
     * @brief Expected error when num_hosts[k] hosts each execute choice k independently: the lowest error level
     *        among the successful executions, or e_fail if they all fail
     * @param ps The probability of success of each choice
     * @param el The error level of each choice; the choices must be sorted by increasing error level
     * @param num_hosts The number of hosts that execute each choice
     * @param e_fail The error level if all executions fail
     * @return The expected error
     */
    double expected_error_on_hosts(const std::vector<double> &ps,
                                   const std::vector<double> &el,
                                   const std::vector<long> &num_hosts,
                                   const double e_fail) {
        double p_all_fail = 1.0;
        double weighted_sum = 0.0;
        double running_prod = 1.0;

        for (size_t k = 0; k < ps.size(); k++) {
            const double p_one_succeeds = 1.0 - std::pow(1.0 - ps[k], num_hosts[k]);

            weighted_sum += el[k] * p_one_succeeds * running_prod;
            running_prod *= (1.0 - p_one_succeeds);
            p_all_fail *= (1.0 - p_one_succeeds);
        }
        return weighted_sum + e_fail * p_all_fail;
    }

    /**
     * @brief Number of hosts, out of num_hosts, that execute each choice. For expected_error, hosts are added one at
     *        a time to the choice that leads to the lowest expected error (with one host: lowest
     *        ps * el + (1 - ps) * e_fail); for the other criteria, all hosts execute the best choice. Ties go to the
     *        earliest choice.
     * @param criterion The criterion: expected_error, probability_success, success_error_ratio or error_level
     * @param ps The probability of success of each choice
     * @param el The error level of each choice (sorted by increasing error level if num_hosts > 1 and the
     *        criterion is expected_error)
     * @param num_hosts The number of hosts
     * @param e_fail The error level if all executions fail
     * @return The number of hosts that execute each choice
     * @throws std::runtime_error if there are no choices
     * @throws std::invalid_argument if the criterion is unknown
     */
    std::vector<long> allocate_hosts(const std::string &criterion,
                                     const std::vector<double> &ps,
                                     const std::vector<double> &el,
                                     const long num_hosts,
                                     const double e_fail) {
        if (ps.empty()) {
            throw std::runtime_error("allocate_hosts(): no options to choose from");
        }
        std::vector<long> allocation(ps.size(), 0);
        size_t best = 0;

        if (criterion == "expected_error") {
            if (num_hosts == 1) {
                // one host: the expected error of a choice is simply ps * el + (1 - ps) * e_fail
                double best_value = std::numeric_limits<double>::infinity();
                for (size_t k = 0; k < ps.size(); k++) {
                    const double value = ps[k] * el[k] + (1.0 - ps[k]) * e_fail;
                    if (value < best_value) {
                        best_value = value;
                        best = k;
                    }
                }
                allocation[best] = 1;
                return allocation;
            }
            // add the hosts one at a time, each to the choice that leads to the lowest expected error
            for (long host = 0; host < num_hosts; host++) {
                double best_value = std::numeric_limits<double>::infinity();
                size_t to_add = 0;
                for (size_t k = 0; k < ps.size(); k++) {
                    allocation[k]++;
                    const double value = expected_error_on_hosts(ps, el, allocation, e_fail);
                    if (value < best_value) {
                        best_value = value;
                        to_add = k;
                    }
                    allocation[k]--;
                }
                allocation[to_add]++;
            }
            return allocation;
        } else if (criterion == "probability_success") {
            double best_value = -std::numeric_limits<double>::infinity();
            for (size_t k = 0; k < ps.size(); k++) {
                if (ps[k] > best_value) {
                    best_value = ps[k];
                    best = k;
                }
            }
        } else if (criterion == "success_error_ratio") {
            double best_value = -std::numeric_limits<double>::infinity();
            for (size_t k = 0; k < ps.size(); k++) {
                const double ratio = ps[k] / el[k];
                if (ratio > best_value) {
                    best_value = ratio;
                    best = k;
                }
            }
        } else if (criterion == "error_level") {
            // among the choices that can succeed; if there are none, the first choice
            const double ps_threshold = 0.0;
            double best_value = std::numeric_limits<double>::infinity();
            for (size_t k = 0; k < ps.size(); k++) {
                if (ps[k] > ps_threshold && el[k] < best_value) {
                    best_value = el[k];
                    best = k;
                }
            }
        } else {
            throw std::invalid_argument("Unknown criterion '" + criterion + "'");
        }

        allocation[best] = num_hosts;
        return allocation;
    }

    /**
     * @brief The choice that a single host executes (allocate_hosts() with one host)
     * @param criterion The criterion: expected_error, probability_success, success_error_ratio or error_level
     * @param ps The probability of success of each choice
     * @param el The error level of each choice
     * @param e_fail The error level if the execution fails
     * @return The index of the chosen choice
     */
    size_t best_choice(const std::string &criterion,
                       const std::vector<double> &ps,
                       const std::vector<double> &el,
                       const double e_fail) {
        const auto allocation = allocate_hosts(criterion, ps, el, 1, e_fail);
        return std::find(allocation.begin(), allocation.end(), 1) - allocation.begin();
    }

    /**
     * @brief Collect all sequences of options (one per task) from a decision node down to the leaves, appending
     *        them to all_combinations
     * @param all_combinations The sequences of options (output)
     * @param node The decision node to start from
     * @param current_path The options chosen so far (working space; empty when called)
     */
    void collect_combinations(std::vector<std::vector<std::string> > &all_combinations,
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

    /**
     * @brief Error level of the output of a sequence of options executed from a decision node, i.e., the
     *        cumulative error factor of the leaf that the sequence leads to
     * @param current_node The decision node to start from
     * @param combo The sequence of options
     * @param relative_task_index The position in combo of the option for current_node's children
     * @return The error level
     * @throws std::runtime_error if an option of the sequence is not a child of the node
     */
    double calculate_error_level_one_host(const ApplicationSpecs::ExecOptionDecisionNode *current_node,
                                          const std::vector<std::string> &combo,
                                          const int relative_task_index) {
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
