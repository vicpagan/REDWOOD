#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include "Discretization.h"
#include "RecursiveFormulae.h"

namespace wrench {
    /**
     * @brief Relative distance between the upper and lower bounds on the optimal expected error for a delta_t
     *        (this sets delta_t to the given value)
     * @param application_specs The application specification
     * @param start_node The decision tree node from which the bounds are computed
     * @param delta_t The delta_t to evaluate
     * @return |upper - lower| / upper
     */
    double Discretization::relative_bound_gap(const std::shared_ptr<ApplicationSpecs> &application_specs,
                                              const ApplicationSpecs::ExecOptionDecisionNode *start_node,
                                              const double delta_t) {
        set_delta_t(delta_t);
        const double lower = compute_optimal_expected_error(application_specs, *this, start_node, true);
        const double upper = compute_optimal_expected_error(application_specs, *this, start_node, false);
        const double gap = std::abs(upper - lower) / upper;
        return gap;
    }

    /**
     * @brief Largest delta_t (found by geometric bisection, to within 1%) for which the lower and upper bounds on
     *        the optimal expected error (Section V-B) are within relative_tolerance of each other. If even
     *        min_delta_t does not achieve the tolerance, prints a warning and returns min_delta_t.
     *        (The bisection assumes the gap grows with delta_t, which is not always the case.)
     * @param application_specs The application specification
     * @param start_node The decision tree node from which the bounds are computed
     * @param relative_tolerance The maximum relative distance between the two bounds
     * @param min_delta_t The smallest delta_t to consider
     * @param max_delta_t The largest delta_t to consider
     * @return The selected delta_t
     */
    double Discretization::compute_best_delta_t(const std::shared_ptr<ApplicationSpecs> &application_specs,
                                                const ApplicationSpecs::ExecOptionDecisionNode *start_node,
                                                const double relative_tolerance,
                                                const double min_delta_t,
                                                const double max_delta_t) {
        if (relative_bound_gap(application_specs, start_node, min_delta_t) >= relative_tolerance) {
            std::cerr << "Warning: even delta_t = " << min_delta_t
                      << " does not achieve the requested relative tolerance " << relative_tolerance
                      << "; using delta_t = " << min_delta_t << std::endl;
            return min_delta_t;
        }

        double lo = min_delta_t;
        double hi = max_delta_t;
        while ((hi - lo) / hi > 0.01) {
            const double mid = std::sqrt(lo * hi);
            if (relative_bound_gap(application_specs, start_node, mid) < relative_tolerance) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        return lo;
    }

    /**
     * @brief delta_t according to the instance's delta_t scheme: "fixed" (parameter = delta_t) or "compute"
     *        (parameter = relative tolerance, delta_t between 1 and the deadline)
     * @param application_specs The application specification
     * @param start_node The decision tree node from which the "compute" scheme evaluates the bounds
     * @return The selected delta_t
     * @throws std::invalid_argument for an unknown scheme
     */
    double Discretization::select_delta_t(const std::shared_ptr<ApplicationSpecs> &application_specs,
                                          const ApplicationSpecs::ExecOptionDecisionNode *start_node) {
        const std::string scheme = application_specs->get_delta_t_scheme();
        if (scheme == "fixed") {
            return application_specs->get_delta_t_parameter();
        }
        if (scheme == "compute") {
            return compute_best_delta_t(application_specs, start_node, application_specs->get_delta_t_parameter(),
                                        1.0, application_specs->get_deadline());
        }
        throw std::invalid_argument("Unknown delta_t_scheme '" + scheme + "'");
    }

    /**
     * @brief Select delta_t according to the instance's delta_t scheme, unless it is already set:
     *        "fixed" (parameter = delta_t) or "compute" (parameter = relative tolerance between the
     *        lower and upper bounds on the optimal expected error, computed from start_node)
     * @param application_specs The application specification
     * @param start_node The decision tree node from which the "compute" scheme evaluates the bounds
     *        (the root of a host's decision tree)
     */
    void Discretization::initialize_delta_t(const std::shared_ptr<ApplicationSpecs> &application_specs,
                                            const ApplicationSpecs::ExecOptionDecisionNode *start_node) {
        if (_delta_t < 0) {
            const double selected_delta_t = select_delta_t(application_specs, start_node);
            // the "compute" search leaves the last trial value set, so set the selected one
            set_delta_t(selected_delta_t);
        }
    }
}
