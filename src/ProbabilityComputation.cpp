#include <cmath>
#include <vector>

#include <wrench-dev.h>

#include "ProbabilityComputation.h"

#include "Utils.h"

WRENCH_LOG_CATEGORY(probability_computation, "Log category for ProbabilityComputation");


double ProbabilityComputation::compute_probability(const double task_time, const double time_to_deadline,
                                                   bool lower_bound) const {
    // Sanity check
    if (_delta_t < 0) {
        throw std::invalid_argument("Use ProbabilityComputation::set_delta_t() to set delta_t to a >0 value");
    }

    // Discretize time
    // const long m = (lower_bound)
    //                    ? static_cast<long>(ceil(task_time / _delta_t)) - 1
    //                    : static_cast<long>(ceil(task_time / _delta_t));
    // const long n = static_cast<long>(ceil(time_to_deadline / _delta_t));
    // const long R = (_restart_overhead > 0.0) ? static_cast<long>(ceil(_restart_overhead / _delta_t)) : 0L;

    const long m = (lower_bound
                        ? floor_division(task_time, _delta_t)
                        : ceiling_division(task_time, _delta_t));
    const long n = (lower_bound
                        ? ceiling_division(time_to_deadline, _delta_t)
                        : floor_division(time_to_deadline, _delta_t));
    const long R = (_restart_overhead > 0.0
                        ? (lower_bound
                               ? floor_division(_restart_overhead, _delta_t)
                               : ceiling_division(_restart_overhead, _delta_t))
                        : 0L);


    // Allocate memoization array
    // TODO: Bound that perhaps? (i.e., only memoize "small" values...)
    WRENCH_INFO("PROB COMP: n = %ld", n);
    WRENCH_INFO("time_to_deadline = %lf", time_to_deadline);
    WRENCH_INFO("_delta_t = %lf", _delta_t);
    auto dp(std::vector<double>(n + 1, 0.0)); // For bottom up iterative

    // Call iterative function
    WRENCH_INFO("Calling compute_probability with m = %ld, n = %ld", m, n);
    return compute_probability(dp, m, n, R);
}

// Bottom up function P(m, n)
double ProbabilityComputation::compute_probability(std::vector<double> &dp, long m, long n, long R) const {
    // Bottom up DP
    for (long i = m; i <= n; ++i) {
        double probability = success_probability(m);
        for (long k = 0; k < m; ++k) {
            probability += fail_probability(k) * dp[std::max(i - k - R - 1, 0L)];
        }
        dp[i] = probability;
    }
    return dp[n];
}

double ProbabilityComputation::compute_probability_midpoint(const double task_time,
                                                            const double time_to_deadline) const {
    double result_upper_bound = compute_probability(task_time, time_to_deadline, false);
    double result_lower_bound = compute_probability(task_time, time_to_deadline, true);
    return ((result_upper_bound + result_lower_bound) / 2);
}

/**
 * @brief Probability that executing a fixed sequence of options (one per remaining task) completes
 *        by the deadline on one PE, with rollback/recovery after failures (computed bottom-up)
 * @param application_specs The application specification
 * @param start_task_index The index of the task of the first option of the sequence
 * @param option_sequence The options, for tasks start_task_index, start_task_index + 1, ...
 * @param input_data_size Size of the first task's input
 * @param input_error_level Error level of the first task's input
 * @param time_to_deadline Time remaining until the deadline (seconds)
 * @param lower_bound Whether to compute the lower bound (instead of the upper bound)
 * @return The probability of success
 */
double ProbabilityComputation::compute_success_probability(const wrench::ApplicationSpecs &application_specs,
                                                           const int start_task_index,
                                                           const std::vector<std::string> &option_sequence,
                                                           const double input_data_size,
                                                           const double input_error_level,
                                                           const double time_to_deadline,
                                                           const bool lower_bound) const {
    const long n_max = time_to_deadline_steps(time_to_deadline, lower_bound);
    const long R = restart_overhead_steps(lower_bound);
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
        steps[k] = task_time_steps(application_specs.get_execution_time(option_functions, x, y), lower_bound);
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
                probability = success_probability(m) * (is_last_task ? 1.0 : P[k + 1][n - m]);
                for (long u = 0; u < m; u++) {
                    probability += fail_probability(u) * P[k][steps_after_failure(n, u, R, lower_bound)];
                }
            }
            P[k][n] = probability;
        }
    }
    return P[0][n_max];
}
