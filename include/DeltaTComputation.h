#ifndef DELTATCOMPUTATION_H
#define DELTATCOMPUTATION_H

#include <memory>

#include "ApplicationSpecs.h"

namespace wrench {
    /**
     * @brief Largest delta_t (found by bisection) for which the lower and upper bounds on the optimal
     *        expected error (Section V-B) are within relative_tolerance of each other
     * @param application_specs The application specification
     * @param relative_tolerance The maximum relative distance between the two bounds
     * @param min_delta_t The smallest delta_t to consider
     * @param max_delta_t The largest delta_t to consider
     * @return delta_t
     * @throws std::runtime_error if even min_delta_t does not achieve the tolerance
     */
    double compute_best_delta_t(const std::shared_ptr<ApplicationSpecs> &application_specs,
                                double relative_tolerance,
                                double min_delta_t,
                                double max_delta_t);

    /**
     * @brief delta_t according to the instance's delta_t scheme:
     *        "fixed" (parameter = delta_t) or "compute" (parameter = relative tolerance)
     * @param application_specs The application specification
     * @return delta_t
     */
    double select_delta_t(const std::shared_ptr<ApplicationSpecs> &application_specs);
}

#endif //DELTATCOMPUTATION_H
