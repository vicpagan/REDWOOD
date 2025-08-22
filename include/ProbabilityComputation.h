#ifndef PROBABILITY_H
#define PROBABILITY_H

#include <vector>

class ProbabilityComputation {
public:
    explicit ProbabilityComputation(const double lambda, const double restart_overhead) {
        _lambda = lambda;
        _restart_overhead = restart_overhead;
        _delta_t = -1.0;
    }

    [[nodiscard]] double compute_probability_midpoint(double task_time, double time_to_deadline) const;

    [[nodiscard]] double compute_probability(double task_time, double time_to_deadline, bool lower_bound) const;

    double compute_best_deltat(double task_time, double time_to_deadline, double precision);

    void set_delta_t(const double delta_t) {
        _delta_t = delta_t;
    }

    [[nodiscard]] double get_delta_t() const {
        return _delta_t;
    }

    [[nodiscard]] double get_lambda() const {
        return _lambda;
    }

private:
    [[nodiscard]] double success_probability(long u) const;

    [[nodiscard]] double fail_probability(long u) const;

    double compute_probability(std::vector<double> &dp, long m, long n, long R) const;

    double _lambda;
    double _restart_overhead;
    double _delta_t;
};

#endif
