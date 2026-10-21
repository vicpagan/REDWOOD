#include "OptimalExpectedError.h"

#include <algorithm>
#include <limits>

#include "Utils.h"

namespace wrench {
    namespace {
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
                                 const ProbabilityComputation &probability_computation,
                                 const ApplicationSpecs::ExecOptionDecisionNode *node,
                                 const int task_index,
                                 const double x,
                                 const double y,
                                 const long max_steps,
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
                    probability_computation.task_time_steps(application_specs.get_execution_time(option_functions, x, y),
                                                            lower_bound),
                    option_functions.at("d_function")(x, y),
                    option_functions.at("e_function")(x, y)
                };
                // The next task can only start if this option can complete in time
                if (!is_last_task && max_steps - execution.steps >= 0) {
                    collect_node_states(application_specs, probability_computation, child.get(), task_index + 1,
                                        execution.output_size, execution.output_error, max_steps - execution.steps,
                                        lower_bound, states);
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
        const long n_max = probability_computation.time_to_deadline_steps(time_to_deadline, lower_bound);
        const long R = probability_computation.restart_overhead_steps(lower_bound);
        const int last_task_index = application_specs.get_num_tasks() - 1;

        std::vector<NodeState> states;
        collect_node_states(application_specs, probability_computation, start_node, start_task_index,
                            input_data_size, input_error_level, n_max, lower_bound, states);

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
                            expected_error += probability_computation.fail_probability(u) * E[ProbabilityComputation::steps_after_failure(
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
}
