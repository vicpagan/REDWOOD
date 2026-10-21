#ifndef OPTIMALEXPECTEDERROR_H
#define OPTIMALEXPECTEDERROR_H

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ApplicationSpecs.h"
#include "ProbabilityComputation.h"

/**
 * Dynamic programs of Section V of the paper, in discretized time (time steps of
 * ProbabilityComputation::get_delta_t() seconds), computed bottom-up.
 *
 * With lower_bound == false, durations are rounded up and the time to the deadline down
 * (pessimistic upper bound on the expected error); with lower_bound == true, the opposite
 * (optimistic lower bound). See Section V-B.
 */
namespace wrench {
    /// For each decision node: for n = 0, 1, ... remaining time steps, the option that minimizes the
    /// expected error and that expected error, i.e., E(i, x, y, n) of Eq. (1). Each node's entries only
    /// cover the time steps that can remain at that node.
    using ExpectedErrorTable = std::map<const ApplicationSpecs::ExecOptionDecisionNode *,
        std::vector<std::pair<std::string, double> > >;

    /**
     * @brief Compute the expected-error table of all executions that start at a node of a decision tree
     * @param application_specs The application specification
     * @param probability_computation Probability computation (with delta_t set)
     * @param start_node The node at which execution starts (its children are the options of the next task)
     * @param start_task_index The index of the next task
     * @param input_data_size Size of the next task's input (x)
     * @param input_error_level Error level of the next task's input (y)
     * @param time_to_deadline Time remaining until the deadline (seconds)
     * @param lower_bound Whether to compute the lower bound (instead of the upper bound)
     * @return The table
     */
    ExpectedErrorTable compute_expected_error_table(const ApplicationSpecs &application_specs,
                                                    const ProbabilityComputation &probability_computation,
                                                    const ApplicationSpecs::ExecOptionDecisionNode *start_node,
                                                    int start_task_index,
                                                    double input_data_size,
                                                    double input_error_level,
                                                    double time_to_deadline,
                                                    bool lower_bound);

    /**
     * @brief Optimal expected error of a whole application execution on one PE (first task, initial
     *        input, full deadline, all execution options)
     * @param application_specs The application specification
     * @param probability_computation Probability computation (with delta_t set)
     * @param lower_bound Whether to compute the lower bound (instead of the upper bound)
     * @return The optimal expected error
     */
    double compute_optimal_expected_error(const std::shared_ptr<ApplicationSpecs> &application_specs,
                                          const ProbabilityComputation &probability_computation,
                                          bool lower_bound);
}

#endif //OPTIMALEXPECTEDERROR_H
