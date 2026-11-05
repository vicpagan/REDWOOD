#ifndef DISCRETIZATION_H
#define DISCRETIZATION_H

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "ApplicationSpecs.h"
#include "Utils.h"

namespace wrench {
    /**
     * Everything tied to the discretization of time in steps of delta_t: selecting delta_t, converting
     * durations to numbers of steps (rounded for the lower or the upper bound, Section V-B), and the
     * per-step success/failure probabilities.
     */
    class Discretization {
    public:
        /**
         * @brief Constructor (delta_t is not set yet: see initialize_delta_t() and set_delta_t())
         * @param application_specs The application specification (for lambda and the restart overhead)
         */
        explicit Discretization(const std::shared_ptr<ApplicationSpecs> &application_specs) {
            _lambda = application_specs->get_lambda();
            _restart_overhead = application_specs->get_restart_overhead();
            _delta_t = -1.0;
        }

        void initialize_delta_t(const std::shared_ptr<ApplicationSpecs> &application_specs,
                                const ApplicationSpecs::ExecOptionDecisionNode *start_node);

        /**
         * @brief Probability of no failure during u steps, i.e., e^(-u * lambda * delta_t) (cached per delta_t)
         * @param u The number of steps
         * @return The probability
         */
        double success_probability(const long u) const {
            while (static_cast<long>(_success_probability_cache.size()) <= u) {
                const auto v = static_cast<long>(_success_probability_cache.size());
                _success_probability_cache.push_back(exp(-static_cast<double>(v) * _lambda * _delta_t));
            }
            return _success_probability_cache[u];
        }

        /**
         * @brief Probability that the first failure happens during step u, i.e., p_u
         * @param u The step
         * @return The probability
         */
        double fail_probability(const long u) const {
            return success_probability(u) - success_probability(u + 1);
        }

        /**
         * @brief Number of steps of an execution: rounded down for the lower bound, up for the upper bound
         * @param time The execution time (in seconds)
         * @param lower_bound Whether to round for the lower bound (instead of the upper bound)
         * @return The number of steps
         */
        long task_time_steps(const double time, const bool lower_bound) const {
            return lower_bound ? floor_division(time, _delta_t) : ceiling_division(time, _delta_t);
        }

        /**
         * @brief Number of steps remaining before the deadline: rounded up for the lower bound, down for the upper bound
         * @param time The time remaining before the deadline (in seconds)
         * @param lower_bound Whether to round for the lower bound (instead of the upper bound)
         * @return The number of steps
         */
        long time_to_deadline_steps(const double time, const bool lower_bound) const {
            return lower_bound ? ceiling_division(time, _delta_t) : floor_division(time, _delta_t);
        }

        /**
         * @brief Number of steps of the restart overhead: rounded down for the lower bound, up for the upper bound
         * @param lower_bound Whether to round for the lower bound (instead of the upper bound)
         * @return The number of steps
         */
        long restart_overhead_steps(const bool lower_bound) const {
            return lower_bound
                       ? floor_division(_restart_overhead, _delta_t)
                       : ceiling_division(_restart_overhead, _delta_t);
        }

        /**
         * @brief Number of steps remaining after a failure during step u (including the restart overhead)
         * @param n The number of steps remaining when the execution started
         * @param u The step during which the failure happens
         * @param R The number of steps of the restart overhead
         * @param lower_bound Whether to count for the lower bound (instead of the upper bound)
         * @return The number of steps remaining (at least 0)
         */
        long steps_after_failure(const long n, const long u, const long R, const bool lower_bound) const {
            if (lower_bound) {
                // lower bound: waste 0 of the current step, but consume at least 1 step
                if (u == 0 && R == 0) {
                    return std::max(n - 1, 0L);
                }
                return std::max(n - u - R, 0L);
            }
            return std::max(n - u - R - 1, 0L);
        }

        /**
         * @brief Set delta_t (and clear the cached success probabilities if it changes)
         * @param delta_t The duration of a time step (in seconds)
         */
        void set_delta_t(const double delta_t) {
            if (delta_t != _delta_t) {
                _success_probability_cache.clear();
            }
            _delta_t = delta_t;
        }

        /**
         * @brief Get delta_t
         * @return The duration of a time step (in seconds), or -1 if it is not set yet
         */
        double get_delta_t() const {
            return _delta_t;
        }

        /**
         * @brief Get the failure rate
         * @return lambda
         */
        double get_lambda() const {
            return _lambda;
        }

    private:
        double relative_bound_gap(const std::shared_ptr<ApplicationSpecs> &application_specs,
                                  const ApplicationSpecs::ExecOptionDecisionNode *start_node,
                                  double delta_t);

        double compute_best_delta_t(const std::shared_ptr<ApplicationSpecs> &application_specs,
                                    const ApplicationSpecs::ExecOptionDecisionNode *start_node,
                                    double relative_tolerance,
                                    double min_delta_t,
                                    double max_delta_t);

        double select_delta_t(const std::shared_ptr<ApplicationSpecs> &application_specs,
                              const ApplicationSpecs::ExecOptionDecisionNode *start_node);

        double _lambda;
        double _restart_overhead;
        double _delta_t;
        mutable std::vector<double> _success_probability_cache;
    };
}

#endif //DISCRETIZATION_H
