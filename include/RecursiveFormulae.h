#ifndef RECURSIVEFORMULAE_H
#define RECURSIVEFORMULAE_H

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ApplicationSpecs.h"
#include "Discretization.h"

/**
 * The recursive formulas on discretized time: the optimal expected error (Eq. 1) and the probability of
 * success of a sequence of options. Durations are converted to numbers of steps for the lower bound
 * (lower_bound = true) or for the upper bound (lower_bound = false) (Section V-B).
 */
namespace wrench {
    /// For each decision tree node, and each number of remaining time steps: the best option and its expected error
    using ExpectedErrorTable = std::map<const ApplicationSpecs::ExecOptionDecisionNode *,
        std::vector<std::pair<std::string, double> > >;

    ExpectedErrorTable compute_expected_error_table(const ApplicationSpecs &application_specs,
                                                    const Discretization &discretization,
                                                    const ApplicationSpecs::ExecOptionDecisionNode *start_node,
                                                    int start_task_index,
                                                    double input_data_size,
                                                    double input_error_level,
                                                    double time_to_deadline,
                                                    bool lower_bound);

    double calculate_expected_error(const ApplicationSpecs &application_specs,
                                    const Discretization &discretization,
                                    int task_index,
                                    double running_input_data_size,
                                    double running_input_error_level,
                                    ExpectedErrorTable &dp,
                                    const ApplicationSpecs::ExecOptionDecisionNode *current_task_node,
                                    long n,
                                    long R,
                                    long deadline,
                                    bool lower_bound);

    double compute_optimal_expected_error(const std::shared_ptr<ApplicationSpecs> &application_specs,
                                          const Discretization &discretization,
                                          const ApplicationSpecs::ExecOptionDecisionNode *root_node,
                                          bool lower_bound);

    double compute_success_probability(const ApplicationSpecs &application_specs,
                                       const Discretization &discretization,
                                       int start_task_index,
                                       const std::vector<std::string> &option_sequence,
                                       double input_data_size,
                                       double input_error_level,
                                       double time_to_deadline,
                                       bool lower_bound);
}

#endif //RECURSIVEFORMULAE_H
