#include "RecursiveFormulae.h"

#include <limits>

namespace wrench {
    /**
     * @brief The expected error E(x, y, n) of the best options, for every node below start_node and every number
     *        of remaining time steps n up to time_to_deadline (Eq. 1)
     * @param application_specs The application specification
     * @param discretization The discretization (delta_t must be set)
     * @param start_node A node of a decision tree stored in application_specs
     * @param start_task_index The index of the task to execute next (a child of start_node)
     * @param input_data_size This is our x
     * @param input_error_level This is our y
     * @param time_to_deadline The remaining time (in seconds)
     * @param lower_bound Whether to compute the lower bound (instead of the upper bound)
     * @return For each node below start_node and each number of remaining steps: the best option and its expected
     *         error ("impossible", -1 for entries that were not needed)
     */
    ExpectedErrorTable compute_expected_error_table(const ApplicationSpecs &application_specs,
                                                    const Discretization &discretization,
                                                    const ApplicationSpecs::ExecOptionDecisionNode *start_node,
                                                    const int start_task_index,
                                                    const double input_data_size,
                                                    const double input_error_level,
                                                    const double time_to_deadline,
                                                    const bool lower_bound) {
        const long n = discretization.time_to_deadline_steps(time_to_deadline, lower_bound);
        const long R = discretization.restart_overhead_steps(lower_bound);

        // compute E for every number of remaining time steps, so that decisions can be looked up later in the
        // execution, when less time remains
        ExpectedErrorTable dp;
        for (long i = 0; i <= n; i++) {
            calculate_expected_error(application_specs, discretization, start_task_index, input_data_size,
                                     input_error_level, dp, start_node, i, R, n, lower_bound);
        }
        return dp;
    }

    /**
     * @brief One term of the table (Eq. 1): the lowest expected error over the options of the task at
     *        current_task_node with n remaining time steps, counting success of the option (then the next tasks)
     *        and failure during each step (then a re-execution); memoized in dp, which also records the best option
     * @param application_specs The application specification
     * @param discretization The discretization (delta_t must be set)
     * @param task_index The index of the task to execute (a child of current_task_node)
     * @param running_input_data_size The input data size of the task (x)
     * @param running_input_error_level The input error level of the task (y)
     * @param dp The memoization table (updated)
     * @param current_task_node The decision tree node (the options of the task are its children)
     * @param n The number of remaining time steps
     * @param R The number of steps of the restart overhead
     * @param deadline The number of steps at the start of the table (sizes each node's row: n ranges over 0..deadline)
     * @param lower_bound Whether to compute the lower bound (instead of the upper bound)
     * @return The lowest expected error (e_fail for options that cannot complete in time)
     */
    double calculate_expected_error(const ApplicationSpecs &application_specs,
                                    const Discretization &discretization,
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
        if (dp.find(current_task_node) == dp.end()) {
            dp[current_task_node] = std::vector<std::pair<std::string, double> >(deadline + 1, std::make_pair("impossible", -1.0));
        }

        if (dp[current_task_node][n] != std::make_pair(std::string("impossible"), -1.0)) {
            return dp[current_task_node][n].second;
        }

        const bool is_last_task = (task_index == application_specs.get_num_tasks() - 1);
        const std::string task_name = application_specs.get_task(task_index);
        std::string best_option;
        double min_expected_error = std::numeric_limits<double>::infinity();

        for (const auto &child: current_task_node->children) {
            const std::string &option_name = child->execution_option;
            const auto &option_functions = application_specs.get_task_functions().at(task_name).at(option_name);

            const long exec_time = discretization.task_time_steps(application_specs.get_execution_time(option_functions, running_input_data_size, running_input_error_level), lower_bound);

            double expected_error;
            if (n < exec_time) {
                expected_error = application_specs.get_e_fail();
            } else {
                double updated_input_size = option_functions.at("d_function")(running_input_data_size, running_input_error_level);
                double updated_error_level = option_functions.at("e_function")(running_input_data_size, running_input_error_level);

                // task success
                if (is_last_task) {
                    expected_error = discretization.success_probability(exec_time) * updated_error_level;
                } else {
                    double next_task_error = calculate_expected_error(
                        application_specs, discretization, task_index + 1, updated_input_size,
                        updated_error_level, dp, child.get(), n - exec_time, R, deadline, lower_bound);
                    expected_error = discretization.success_probability(exec_time) * next_task_error;
                }

                // task failure during time step u, followed by a re-execution
                for (long u = 0; u < exec_time; u++) {
                    double retry_task_error = calculate_expected_error(
                        application_specs, discretization, task_index, running_input_data_size,
                        running_input_error_level, dp, current_task_node,
                        discretization.steps_after_failure(n, u, R, lower_bound), R, deadline, lower_bound);
                    expected_error += discretization.fail_probability(u) * retry_task_error;
                }
            }

            if (expected_error < min_expected_error) {
                min_expected_error = expected_error;
                best_option = option_name;
            }
        }

        dp[current_task_node][n] = std::make_pair(best_option, min_expected_error);
        return min_expected_error;
    }

    /**
     * @brief The optimal expected error of the whole application: from the root of a stored decision tree,
     *        with the initial data size and error level, and the deadline
     * @param application_specs The application specification
     * @param discretization The discretization (delta_t must be set)
     * @param root_node The root of a decision tree stored in application_specs
     * @param lower_bound Whether to compute the lower bound (instead of the upper bound)
     * @return The optimal expected error
     */
    double compute_optimal_expected_error(const std::shared_ptr<ApplicationSpecs> &application_specs,
                                          const Discretization &discretization,
                                          const ApplicationSpecs::ExecOptionDecisionNode *root_node,
                                          const bool lower_bound) {
        const auto dp = compute_expected_error_table(*application_specs, discretization, root_node, 0,
                                                     application_specs->get_initial_data_size(),
                                                     application_specs->get_initial_error_level(),
                                                     application_specs->get_deadline(),
                                                     lower_bound);
        return dp.at(root_node).back().second;
    }

    /**
     * @brief Probability that a sequence of options, for the tasks starting at start_task_index, all complete
     *        before the deadline on one host. After a failure, the same option is re-executed. The sequence ends
     *        with its last option: with a single option, this is the probability of success of that option alone
     *        (nearsighted); with options for all remaining tasks, the probability that the application completes
     *        (foresighted).
     * @param application_specs The application specification
     * @param discretization The discretization (delta_t must be set)
     * @param start_task_index The index of the task of the first option
     * @param option_sequence One option per task, starting at start_task_index
     * @param input_data_size The input data size of the first task (x)
     * @param input_error_level The input error level of the first task (y)
     * @param time_to_deadline The remaining time (in seconds)
     * @param lower_bound Whether to compute the lower bound (instead of the upper bound)
     * @return The probability of success (0 if the deadline has passed or the sequence is empty)
     */
    double compute_success_probability(const ApplicationSpecs &application_specs,
                                       const Discretization &discretization,
                                       const int start_task_index,
                                       const std::vector<std::string> &option_sequence,
                                       const double input_data_size,
                                       const double input_error_level,
                                       const double time_to_deadline,
                                       const bool lower_bound) {
        const long n_max = discretization.time_to_deadline_steps(time_to_deadline, lower_bound);
        const long R = discretization.restart_overhead_steps(lower_bound);
        const size_t num_options = option_sequence.size();

        if (n_max < 0 || num_options == 0) {
            return 0.0;
        }

        // number of steps of each execution, and maximum number of steps remaining before each one
        std::vector<long> steps(num_options);
        std::vector<long> max_steps(num_options);
        double x = input_data_size;
        double y = input_error_level;
        long remaining = n_max;
        for (size_t k = 0; k < num_options; k++) {
            const auto &option_functions = application_specs.get_task_functions().at(application_specs.get_task(start_task_index + static_cast<int>(k))).at(option_sequence[k]);
            steps[k] = discretization.task_time_steps(application_specs.get_execution_time(option_functions, x, y), lower_bound);
            max_steps[k] = remaining;
            remaining -= steps[k];
            const double next_x = option_functions.at("d_function")(x, y);
            y = option_functions.at("e_function")(x, y);
            x = next_x;
        }

        // P[k][n]: probability that executions k, k+1, ... all complete within n steps.
        // P[k] only depends on P[k + 1] and on P[k][n' < n]: fill it from the last execution backwards.
        std::vector<std::vector<double> > P(num_options);
        for (size_t k = num_options; k-- > 0;) {
            if (max_steps[k] < 0) {
                continue; // the previous executions cannot complete in time
            }
            const long m = steps[k];
            const bool is_last_option = (k == num_options - 1);
            P[k].resize(max_steps[k] + 1);
            for (long n = 0; n <= max_steps[k]; n++) {
                double probability;
                if (n < m) {
                    probability = 0.0;
                } else {
                    probability = discretization.success_probability(m) * (is_last_option ? 1.0 : P[k + 1][n - m]);
                    for (long u = 0; u < m; u++) {
                        probability += discretization.fail_probability(u) * P[k][discretization.steps_after_failure(n, u, R, lower_bound)];
                    }
                }
                P[k][n] = probability;
            }
        }
        return P[0][n_max];
    }
}
