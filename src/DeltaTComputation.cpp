#include "DeltaTComputation.h"

#include <cmath>
#include <stdexcept>
#include <string>

#include <wrench-dev.h>

#include "OptimalExpectedError.h"
#include "ProbabilityComputation.h"

WRENCH_LOG_CATEGORY(delta_t_computation, "Log category for DeltaTComputation");

namespace wrench {
    namespace {
        /// Relative distance between the upper and lower bounds on the optimal expected error
        double relative_bound_gap(const std::shared_ptr<ApplicationSpecs> &application_specs, const double delta_t) {
            ProbabilityComputation probability_computation(application_specs);
            probability_computation.set_delta_t(delta_t);
            const double lower = compute_optimal_expected_error(application_specs, probability_computation, true);
            const double upper = compute_optimal_expected_error(application_specs, probability_computation, false);
            const double gap = std::abs(upper - lower) / upper;
            WRENCH_INFO("delta_t = %lf: lower bound = %lf, upper bound = %lf, relative gap = %lf",
                        delta_t, lower, upper, gap);
            return gap;
        }
    }

    double compute_best_delta_t(const std::shared_ptr<ApplicationSpecs> &application_specs,
                                const double relative_tolerance,
                                const double min_delta_t,
                                const double max_delta_t) {
        if (relative_bound_gap(application_specs, min_delta_t) >= relative_tolerance) {
            throw std::runtime_error("compute_best_delta_t(): even delta_t = " + std::to_string(min_delta_t) +
                                     " does not achieve the requested tolerance");
        }

        double lo = min_delta_t;
        double hi = max_delta_t;
        while ((hi - lo) / hi > 0.01) {
            const double mid = std::sqrt(lo * hi);
            if (relative_bound_gap(application_specs, mid) < relative_tolerance) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        WRENCH_INFO("Selected delta_t = %lf", lo);
        return lo;
    }

    double select_delta_t(const std::shared_ptr<ApplicationSpecs> &application_specs) {
        const std::string scheme = application_specs->get_delta_t_scheme();
        if (scheme == "fixed") {
            return application_specs->get_delta_t_parameter();
        }
        if (scheme == "compute") {
            return compute_best_delta_t(application_specs, application_specs->get_delta_t_parameter(),
                                        1.0, application_specs->get_deadline());
        }
        throw std::invalid_argument("Unknown delta_t_scheme '" + scheme + "'");
    }
}
