#include "DynamicProgramming.h"

#include <algorithm>
#include <limits>

#include "Utils.h"

namespace wrench {
    namespace {
        // Discretization (Section V-B): rounding is done so as to obtain either bound

        long task_time_steps(const double time, const double delta_t, const bool lower_bound) {
            return lower_bound ? floor_division(time, delta_t) : ceiling_division(time, delta_t);
        }

        long time_to_deadline_steps(const double time, const double delta_t, const bool lower_bound) {
            return lower_bound ? ceiling_division(time, delta_t) : floor_division(time, delta_t);
        }

        long restart_overhead_steps(const double time, const double delta_t, const bool lower_bound) {
            return lower_bound ? floor_division(time, delta_t) : ceiling_division(time, delta_t);
        }

        /// Time steps remaining after a failure during time step u of an execution that started with n
        /// steps remaining, given a restart overhead of R steps. For the lower bound, a failure during
        /// the first step still consumes one step when R == 0, so that the recursion is well-founded.
        long steps_after_failure(const long n, const long u, const long R, const bool lower_bound) {
            if (lower_bound) {
                if (u == 0 && R == 0) {
                    return std::max(n - 1, 0L);
                }
                return std::max(n - u - R, 0L);
            }
            return std::max(n - u - R - 1, 0L);
        }

        /// Read + compute + write time of an option on input (x, y)
        double execution_time(const std::map<std::string, std::function<double(double, double)> > &option_functions,
                              const ApplicationSpecs &application_specs, const double x, const double y) {
            return (x / application_specs.get_io_read_bandwidth_per_node()) + option_functions.at("t_function")(x, y) +
                   (option_functions.at("d_function")(x, y) / application_specs.get_io_write_bandwidth_per_node());
        }

        /// An option of a task, executed on a given input
        struct OptionExecution {
            const ApplicationSpecs::ExecOptionDecisionNode *node; ///< decision node reached by executing it
            std::string option;
            long steps; ///< number of time steps of the execution
            double output_size; ///< size of its output
            double output_error; ///< error level of its output
        };

        /// A decision node at which execution can be, with the input of the next task
        struct NodeState {
            const ApplicationSpecs::ExecOptionDecisionNode *node;
            int task_index; ///< index of the next task
            long max_steps; ///< maximum number of time steps that can remain at this node
            std::vector<OptionExecution> options; ///< options of the next task
        };

        /// Append, in post order (children before parents), the nodes at which execution can be when starting
        /// at node with max_steps time steps remaining. These are the states visited by the recursive formula.
        void collect_node_states(const ApplicationSpecs &application_specs,
                                 const ApplicationSpecs::ExecOptionDecisionNode *node,
                                 const int task_index,
                                 const double x,
                                 const double y,
                                 const long max_steps,
                                 const double delta_t,
                                 const bool lower_bound,
                                 std::vector<NodeState> &states) {
            const bool is_last_task = (task_index == application_specs.get_num_tasks() - 1);
            const std::string task = application_specs.get_task(task_index);

            NodeState state{node, task_index, max_steps, {}};
            for (const auto &child: node->children) {
                const auto &option_functions = application_specs.get_task_functions().at(task).at(
                    child->execution_option);
                OptionExecution execution{
                    child.get(), child->execution_option,
                    task_time_steps(execution_time(option_functions, application_specs, x, y), delta_t, lower_bound),
                    option_functions.at("d_function")(x, y),
                    option_functions.at("e_function")(x, y)
                };
                // The next task can only start if this option can complete in time
                if (!is_last_task && max_steps - execution.steps >= 0) {
                    collect_node_states(application_specs, child.get(), task_index + 1, execution.output_size,
                                        execution.output_error, max_steps - execution.steps, delta_t, lower_bound,
                                        states);
                }
                state.options.push_back(std::move(execution));
            }
            states.push_back(std::move(state));
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
        const double delta_t = probability_computation.get_delta_t();
        const long n_max = time_to_deadline_steps(time_to_deadline, delta_t, lower_bound);
        const long R = restart_overhead_steps(application_specs.get_restart_overhead(), delta_t, lower_bound);
        const int last_task_index = application_specs.get_num_tasks() - 1;

        std::vector<NodeState> states;
        collect_node_states(application_specs, start_node, start_task_index, input_data_size, input_error_level,
                            n_max, delta_t, lower_bound, states);

        // Children are before parents in states, and E(node, n) only depends on E(child, n' <= n) and
        // on E(node, n' < n): fill the table node by node, by increasing n
        ExpectedErrorTable table;
        for (const auto &state: states) {
            auto &E = table[state.node];
            E.resize(state.max_steps + 1);

            for (long n = 0; n <= state.max_steps; n++) {
                std::string best_option;
                double min_expected_error = std::numeric_limits<double>::infinity();

                for (const auto &execution: state.options) {
                    const long m = execution.steps;
                    double expected_error;

                    if (n < m) {
                        // Cannot complete before the deadline
                        expected_error = application_specs.get_e_fail();
                    } else {
                        // Task success
                        if (state.task_index == last_task_index) {
                            expected_error = probability_computation.success_probability(m) * execution.output_error;
                        } else {
                            expected_error = probability_computation.success_probability(m) * table.at(execution.node)[
                                                 n - m].second;
                        }
                        // Task failure during time step u, followed by a re-execution
                        for (long u = 0; u < m; u++) {
                            expected_error += probability_computation.fail_probability(u) * E[steps_after_failure(
                                n, u, R, lower_bound)].second;
                        }
                    }

                    if (expected_error < min_expected_error) {
                        min_expected_error = expected_error;
                        best_option = execution.option;
                    }
                }
                E[n] = std::make_pair(best_option, min_expected_error);
            }
        }
        return table;
    }

    double compute_optimal_expected_error(const std::shared_ptr<ApplicationSpecs> &application_specs,
                                          const ProbabilityComputation &probability_computation,
                                          const bool lower_bound) {
        // A full decision tree (i.e., with all option sequences)
        ApplicationSpecs::ExecOptionDecisionTree tree(application_specs.get(),
                                                      ApplicationSpecs::ExecOptionDecisionNode::create_decision_node(
                                                          "", "", false));
        tree.build_tree();

        const auto table = compute_expected_error_table(*application_specs, probability_computation, tree.root.get(), 0,
                                                        application_specs->get_initial_data_size(),
                                                        application_specs->get_initial_error_level(),
                                                        application_specs->get_deadline(), lower_bound);
        return table.at(tree.root.get()).back().second;
    }

    double compute_success_probability(const ApplicationSpecs &application_specs,
                                       const ProbabilityComputation &probability_computation,
                                       const int start_task_index,
                                       const std::vector<std::string> &option_sequence,
                                       const double input_data_size,
                                       const double input_error_level,
                                       const double time_to_deadline,
                                       const bool lower_bound) {
        const double delta_t = probability_computation.get_delta_t();
        const long n_max = time_to_deadline_steps(time_to_deadline, delta_t, lower_bound);
        const long R = restart_overhead_steps(application_specs.get_restart_overhead(), delta_t, lower_bound);
        const int last_task_index = application_specs.get_num_tasks() - 1;
        const size_t num_options = option_sequence.size();

        // Number of steps of each execution, and maximum number of steps remaining before each one
        std::vector<long> steps(num_options);
        std::vector<long> max_steps(num_options);
        double x = input_data_size;
        double y = input_error_level;
        long remaining = n_max;
        for (size_t k = 0; k < num_options; k++) {
            const auto &option_functions = application_specs.get_task_functions()
                    .at(application_specs.get_task(start_task_index + static_cast<int>(k))).at(option_sequence[k]);
            steps[k] = task_time_steps(execution_time(option_functions, application_specs, x, y), delta_t, lower_bound);
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
                continue; // Unreachable: the previous executions cannot complete in time
            }
            const long m = steps[k];
            const bool is_last_task = (start_task_index + static_cast<int>(k) == last_task_index);
            P[k].resize(max_steps[k] + 1);
            for (long n = 0; n <= max_steps[k]; n++) {
                double probability;
                if (n < m) {
                    probability = 0.0;
                } else {
                    probability = probability_computation.success_probability(m) * (
                                      is_last_task ? 1.0 : P[k + 1][n - m]);
                    for (long u = 0; u < m; u++) {
                        probability += probability_computation.fail_probability(u) * P[k][steps_after_failure(
                            n, u, R, lower_bound)];
                    }
                }
                P[k][n] = probability;
            }
        }
        return P[0][n_max];
    }
}
