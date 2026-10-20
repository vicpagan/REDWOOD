#ifndef PROBABILITY_H
#define PROBABILITY_H

#include <cmath>
#include <vector>

#include "ApplicationSpecs.h"

class ProbabilityComputation {
public:
    explicit ProbabilityComputation(const std::shared_ptr<wrench::ApplicationSpecs> &application_specs) {
        _lambda = application_specs->get_lambda();
        _restart_overhead = application_specs->get_restart_overhead();
        _delta_t = -1.0;
    }

    // This is e^(-u * lambda * delta_t)
    // Values are cached because the dynamic programs request the same few values millions of times
    [[nodiscard]] double success_probability(const long u) const {
        while (static_cast<long>(_success_probability_cache.size()) <= u) {
            const auto v = static_cast<long>(_success_probability_cache.size());
            _success_probability_cache.push_back(exp(-static_cast<double>(v) * _lambda * _delta_t));
        }
        return _success_probability_cache[u];
    }

    // This is p_u
    [[nodiscard]] double fail_probability(const long u) const {
        return success_probability(u) - success_probability(u + 1);
    }

    [[nodiscard]] double compute_probability_midpoint(double task_time, double time_to_deadline) const;

    [[nodiscard]] double compute_probability(double task_time, double time_to_deadline, bool lower_bound) const;

    [[nodiscard]] double compute_success_probability(const ApplicationSpecs &application_specs,
                                                     int start_task_index,
                                                     const std::vector<std::string> &option_sequence,
                                                     double input_data_size,
                                                     double input_error_level,
                                                     double time_to_deadline,
                                                     bool lower_bound) const;

    void set_delta_t(const double delta_t) {
        if (delta_t != _delta_t) {
            _success_probability_cache.clear();
        }
        _delta_t = delta_t;
    }

    [[nodiscard]] double get_delta_t() const {
        return _delta_t;
    }

    [[nodiscard]] double get_lambda() const {
        return _lambda;
    }

private:
    double compute_probability(std::vector<double> &dp, long m, long n, long R) const;

    double _lambda;
    double _restart_overhead;
    double _delta_t;
    mutable std::vector<double> _success_probability_cache;
};

#endif
