#include <cmath>
#include <iostream>
#include <vector>

#include "ProbabilityComputation.h"


// This is e^(-u * lambda * delta_t)
double inline ProbabilityComputation::success_probability(const long u) const {
    return exp(-static_cast<double>(u) * _lambda * _delta_t);
}

// This is p_u
double inline ProbabilityComputation::fail_probability(const long u) const {
    return success_probability(u) - success_probability(u + 1);
}

double ProbabilityComputation::compute_probability(const double task_time, const double time_to_deadline,
                                                   bool lower_bound) const {
    // Sanity check
    if (_delta_t < 0) {
        throw std::invalid_argument("Use ProbabilityComputation::set_delta_t() to set delta_t to a >0 value");
    }

    // Discretize time
    const long m = (lower_bound)
                       ? static_cast<long>(ceil(task_time / _delta_t)) - 1
                       : static_cast<long>(ceil(task_time / _delta_t));
    const long n = static_cast<long>(ceil(time_to_deadline / _delta_t));
    const long R = (_restart_overhead > 0.0) ? static_cast<long>(ceil(_restart_overhead / _delta_t)) : 0L;

    // Allocate memoization array
    // TODO: Bound that perhaps? (i.e., only memoize "small" values...)
    // std::cerr << "PROB COMP: n = " << n << std::endl;
    // std::cerr << "time_to_deadline = " << time_to_deadline << std::endl;
    // std::cerr << "_delta_t = " << _delta_t << std::endl;
    auto dp(std::vector<double>(n + 1, 0.0)); // For bottom up iterative

    // Call iterative function
    // std::cerr << "Calling compute_probability with m = " << m << ", n = " << n << std::endl;
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

