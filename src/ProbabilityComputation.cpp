#include <cmath>
#include <iostream>
#include <vector>

#include <ProbabilityComputation.h>

double epsilon = 1e-6;


// Relative closeness check with a small epsilon
bool ProbabilityComputation::is_equal(double a, double b) {
    return fabs(a - b) < epsilon * std::max(fabs(a), fabs(b));
}

// This is e^(-u * lambda * delta_t)
double inline ProbabilityComputation::success_probability(const long u) const {
    return exp(-static_cast<double>(u) * lambda * delta_t);
}

// This is p_u
double inline ProbabilityComputation::fail_probability(const long u) const {
    return success_probability(u) - success_probability(u + 1);
}

double ProbabilityComputation::compute_probability(const double task_time, const double time_to_deadline) const {
    // Sanity check
    if (this->delta_t < 0) {
        throw std::invalid_argument("Use ProbabilityComputation::set_delta_t() to set delta_t to a >0 value");
    }

    // Discretize time
    const long m = ceil(task_time / this->delta_t);
    const long n = ceil(time_to_deadline / this->delta_t);

    // Allocate memoization array
    // TODO: Bound that perhaps? (i.e., only memoize "small" values...)
    auto dp(std::vector<double>(n + 1, -1.0));

    // Call recursive function
    return compute_probability(dp, m, n);
}

// Bottom up recursive function P(m, n)
// Can edit to account for restart overhead if necessary
double ProbabilityComputation::compute_probability(std::vector<double>& dp, long m, long n) const {

    // Memoization
    if (dp[n] >= 0.0) {
        return dp[n];
    }

    // Base case
    if (n < m) {
        dp[n] = 0.0;
        return 0.0;
    }

    // Recursion
    double probability = success_probability(m);
    for (int k = 0; k <= m - 1; ++k) {
        probability += fail_probability(k) * compute_probability(dp, m,
            std::max(0L, n - k - 1 - static_cast<long>(this->restart_overhead / this->delta_t)));
    }

    // Memoization
    dp[n] = probability;
    return probability;
}

double ProbabilityComputation::compute_best_deltat(const double task_time, double time_to_deadline, double precision) {
    // Remember the current delta to restore it later (pretty hacky)
    double current_delta_t = this->delta_t;

    double lo = task_time / 1000.0; // What to put here?
    double hi = task_time / 1.0;  // What to put here?
    double best_deltat = hi;

    while (std::abs(hi - lo) / hi  > precision) {
        // std::cerr << "HI=" << hi << "  LO=" <<  lo << std::endl;
        double mid = (lo + hi) / 2;
        this->delta_t = mid;
        double result_mid = compute_probability(task_time, time_to_deadline);
        this->delta_t = mid/2;
        double result_half = compute_probability(task_time, time_to_deadline);
        // std::cerr << "PROB(MID) = " << result_mid << "   PROB(HALF) = " << result_half << std::endl;

        if ((std::abs(result_mid - result_half) / result_mid) < precision) {
            // Precision is good enough — try a larger deltat
            best_deltat = mid;
            lo = mid;
        } else {
            // Not precise enough — reduce deltat
            hi = mid;
        }
    }

    // Restore original delta_t
    this->delta_t = current_delta_t;

    return best_deltat;

}


