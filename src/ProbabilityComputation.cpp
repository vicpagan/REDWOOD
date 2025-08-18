#include <cmath>
#include <iostream>
#include <vector>

#include <ProbabilityComputation.h>


// This is e^(-u * lambda * delta_t)
double inline ProbabilityComputation::success_probability(const long u) const {
    return exp(-static_cast<double>(u) * lambda * delta_t);
}

// This is p_u
double inline ProbabilityComputation::fail_probability(const long u) const {
    return success_probability(u) - success_probability(u + 1);
}

double ProbabilityComputation::compute_probability(const double task_time, const double time_to_deadline, bool lower_bound) const {
    // Sanity check
    if (this->delta_t < 0) {
        throw std::invalid_argument("Use ProbabilityComputation::set_delta_t() to set delta_t to a >0 value");
    }

    // Discretize time
    const long m = (lower_bound) ? static_cast<long>(ceil(task_time / this->delta_t)) - 1 : static_cast<long>(ceil(task_time / this->delta_t));
    const long n = static_cast<long>(ceil(time_to_deadline / this->delta_t));
    const long R = (this->restart_overhead > 0.0) ? static_cast<long>(ceil(this->restart_overhead / this->delta_t)) : 0L;

    // Allocate memoization array
    // TODO: Bound that perhaps? (i.e., only memoize "small" values...)
    // auto dp(std::vector<double>(n + 1, -1.0)); // For memoized recursive
    auto dp(std::vector<double>(n + 1, 0.0)); // For bottom up iterative

    // Call recursive function
    // std::cerr << "Calling compute_probability with m = " << m << ", n = " << n << std::endl;
    return compute_probability(dp, m, n, R);
}

// Bottom up function P(m, n)
double ProbabilityComputation::compute_probability(std::vector<double>& dp, long m, long n, long R) const {

    // // Memoization
    // if (dp[n] >= 0.0) {
    //     return dp[n];
    // }
    //
    // // Base case
    // if (n < m) {
    //     dp[n] = 0.0;
    //     return 0.0;
    // }
    //
    // // Recursion
    // double probability = success_probability(m);
    // for (int k = 0; k <= m - 1; ++k) {
    //     probability += fail_probability(k) * compute_probability(dp, m,
    //         std::max(0L, n - k - R - 1), R);
    // }
    //
    // // Memoization
    // dp[n] = probability;
    // return probability;

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

double ProbabilityComputation::compute_best_deltat(const double task_time, double time_to_deadline, double precision) {
    // Remember the current delta to restore it later (pretty hacky)
    double current_delta_t = this->delta_t;

    double lo = task_time / 1000.0; // What to put here?
    double hi = task_time / 1.0;  // What to put here?
    double best_deltat = hi;

    // while (std::abs(hi - lo) / hi  > precision) {
    //     // std::cerr << "HI=" << hi << "  LO=" <<  lo << std::endl;
    //     double mid = (lo + hi) / 2;
    //
    //     std::chrono::duration<double, std::nano> duration_mid = std::chrono::duration<double, std::nano>::zero();
    //     std::chrono::duration<double, std::nano> duration_half = std::chrono::duration<double, std::nano>::zero();
    //
    //     this->delta_t = mid;
    //     auto start_mid = std::chrono::high_resolution_clock::now();
    //     double result_mid_upper_bound = compute_probability(task_time, time_to_deadline);
    //     double result_mid_lower_bound = compute_probability((task_time - 1), time_to_deadline);
    //     auto end_mid = std::chrono::high_resolution_clock::now();
    //     double result_mid = (result_mid_upper_bound + result_mid_lower_bound) / 2;
    //     duration_mid += end_mid - start_mid;
    //
    //     this->delta_t = mid/2;
    //     auto start_half = std::chrono::high_resolution_clock::now();
    //     double result_half_upper_bound = compute_probability(task_time, time_to_deadline);
    //     double result_half_lower_bound = compute_probability((task_time - 1), time_to_deadline);
    //     auto end_half = std::chrono::high_resolution_clock::now();
    //     double result_half = (result_half_upper_bound + result_half_lower_bound) / 2;
    //     duration_half += end_half - start_half;
    //     std::cerr << "PROB(MID) UPPER BOUND = " << result_mid_upper_bound <<
    //         "   PROB(MID) LOWER BOUND = " << result_mid_lower_bound <<
    //         "   PROB(MID) AVG = " << result_mid << std::endl;
    //     std::cerr << "PROB(HALF) UPPER BOUND = " << result_half_upper_bound <<
    //         "   PROB(HALF) LOWER BOUND = " << result_half_lower_bound <<
    //         "   PROB(HALF) AVG = " << result_half << std::endl;
    //
    //     if ((std::abs(result_mid - result_half) / result_mid) < precision) {
    //         // Precision is good enough — try a larger deltat
    //         best_deltat = mid;
    //         lo = mid;
    //     } else {
    //         // Not precise enough — reduce deltat
    //         hi = mid;
    //     }
    // }

    while (std::abs(hi - lo) / hi  > precision) {
        std::cerr << "HI=" << hi << "  LO=" <<  lo << std::endl;
        double mid = (lo + hi) / 2;

        this->delta_t = mid;

        double result_upper_bound = compute_probability(task_time, time_to_deadline, false);
        double result_lower_bound = compute_probability(task_time, time_to_deadline, true);
        double result_avg = (result_upper_bound + result_lower_bound) / 2;

        std::cerr << "PROB(MID) UPPER BOUND = " << result_upper_bound <<
            "   PROB(MID) LOWER BOUND = " << result_lower_bound <<
            "   PROB(MID) AVG = " << result_avg << std::endl;

        if ((std::abs(result_upper_bound - result_lower_bound) / result_upper_bound) < precision) {
            // Precision is good enough — try a larger deltat
            std::cerr << "Precision is good enough - trying a larger deltat    Current deltat " << mid << std::endl << std::endl;
            best_deltat = mid;
            lo = mid;
        } else {
            // Not precise enough — reduce deltat
            std::cerr << "Not precise enough - reducing deltat    Current deltat = " << mid << std::endl << std::endl;
            hi = mid;
        }
    }

    // Restore original delta_t
    this->delta_t = current_delta_t;

    return best_deltat;

}


