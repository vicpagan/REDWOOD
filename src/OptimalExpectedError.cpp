#include "OptimalExpectedError.h"

#include <limits>

namespace wrench {
    namespace {
        /**
         * @brief The recursive formula of Eq. (1): E(i, x, y, n), memoized in dp
         * @param application_specs The application specification
         * @param probability_computation Probability computation (with delta_t set)
         * @param task_index The index of the next task (i)
         * @param running_input_data_size The size of its input (x)
         * @param running_input_error_level The error level of its input (y)
         * @param dp The memoization table
         * @param current_task_node The current decision node (its children are the options of the next task)
         * @param n The number of remaining time steps
         * @param R The restart overhead in time steps
         * @param deadline The number of time steps until the deadline (size of the table)
         * @param lower_bound Whether to compute the lower bound (instead of the upper bound)
         * @return The minimum expected error
         */
        double calculate_expected_error(const ApplicationSpecs &application_specs,
                                        const ProbabilityComputation &probability_computation,
                                        const int task_index,
                                        const double running_input_data_size,
                                        const double running_input_error_level,
                                        ExpectedErrorTable &dp,
                                        const ApplicationSpecs::ExecOptionDecisionNode *current_task_node,
                                        const long n,
                                        const long R,
                                        const long deadline,
                                        const bool lower_bound) {
            // NOTE: simpler than initializing it all at once which requires tree traversal
            // (single map lookup, since this runs for every term of the recursion)
            auto &dp_node = dp.try_emplace(current_task_node, deadline + 1, std::make_pair("impossible", -1.0)).first->
                    second;

            if (dp_node[n].second != -1.0 || dp_node[n].first != "impossible") {
                return dp_node[n].second;
            }

            const bool is_last_task = (task_index == application_specs.get_num_tasks() - 1);
            const std::string task_name = application_specs.get_task(task_index);
            std::string best_option;
            double min_expected_error = std::numeric_limits<double>::infinity();

            for (const auto &child: current_task_node->children) {
                const std::string &option_name = child->execution_option;
                const auto &option_functions = application_specs.get_task_functions().at(task_name).at(option_name);

                const long exec_time = probability_computation.task_time_steps(
                    application_specs.get_execution_time(option_functions, running_input_data_size,
                                                         running_input_error_level), lower_bound);
                double expected_error;

                if (n < exec_time) {
                    expected_error = application_specs.get_e_fail();
                } else {
                    double updated_input_size = option_functions.at("d_function")(
                        running_input_data_size, running_input_error_level);
                    double updated_error_level = option_functions.at("e_function")(
                        running_input_data_size, running_input_error_level);

                    // Task success
                    if (is_last_task) {
                        expected_error = probability_computation.success_probability(exec_time) * updated_error_level;
                    } else {
                        double next_task_error = calculate_expected_error(
                            application_specs, probability_computation, task_index + 1, updated_input_size,
                            updated_error_level, dp, child.get(), n - exec_time, R, deadline, lower_bound);
                        expected_error = probability_computation.success_probability(exec_time) * next_task_error;
                    }

                    // Task failure during time step u, followed by a re-execution
                    for (long u = 0; u < exec_time; u++) {
                        double retry_task_error = calculate_expected_error(
                            application_specs, probability_computation, task_index, running_input_data_size,
                            running_input_error_level, dp, current_task_node,
                            ProbabilityComputation::steps_after_failure(n, u, R, lower_bound), R, deadline,
                            lower_bound);
                        expected_error += probability_computation.fail_probability(u) * retry_task_error;
                    }
                }

                if (expected_error < min_expected_error) {
                    min_expected_error = expected_error;
                    best_option = option_name;
                }
            }

            dp_node[n] = std::make_pair(best_option, min_expected_error);
            return min_expected_error;
        }
    }

    ExpectedErrorTable compute_expected_error_table(const ApplicationSpecs &application_specs,
                                                    const ProbabilityComputation &probability_computation,
                                                    const ApplicationSpecs::ExecOptionDecisionNode *start_node,
                                                    const int start_task_index,
                                                    const double input_data_size,
                                                    const double input_error_level,
                                                    const double time_to_deadline,
                                                    const bool lower_bound) {
        const long n = probability_computation.time_to_deadline_steps(time_to_deadline, lower_bound);
        const long R = probability_computation.restart_overhead_steps(lower_bound);

        // Compute E for every number of remaining time steps, so that decisions can be looked up later in the
        // execution, when less time remains
        ExpectedErrorTable dp;
        for (long i = 0; i <= n; i++) {
            calculate_expected_error(application_specs, probability_computation, start_task_index, input_data_size,
                                     input_error_level, dp, start_node, i, R, n, lower_bound);
        }
        return dp;
    }

    double compute_optimal_expected_error(const std::shared_ptr<ApplicationSpecs> &application_specs,
                                          const ProbabilityComputation &probability_computation,
                                          const bool lower_bound) {
        // A full decision tree (i.e., with all option sequences)
        ApplicationSpecs::ExecOptionDecisionTree tree(application_specs.get(),
                                                      ApplicationSpecs::ExecOptionDecisionNode::create_decision_node(
                                                          "", "", false));
        tree.build_tree();

        const auto dp = compute_expected_error_table(*application_specs, probability_computation, tree.root.get(), 0,
                                                     application_specs->get_initial_data_size(),
                                                     application_specs->get_initial_error_level(),
                                                     application_specs->get_deadline(), lower_bound);
        return dp.at(tree.root.get()).back().second;
    }
}
