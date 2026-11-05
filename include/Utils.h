#ifndef UTILS_H
#define UTILS_H

#include <boost/json.hpp>

boost::json::object readJSONFromFile(const std::string& filepath);

/**
 * @brief Calculates the ceiling of a division between two floating point numbers, treating a quotient within
 *        1e-9 of an integer as that integer
 * @param numerator The numerator
 * @param denominator The denominator
 * @return The rounded-up quotient
 */
inline long ceiling_division(const double numerator, const double denominator) {
    const double result = numerator / denominator;
    // if very close to an integer, use that integer instead of ceiling
    double rounded = std::round(result);
    if (std::abs(result - rounded) < 1e-9) {
        return static_cast<long>(rounded);
    }
    return static_cast<long>(std::ceil(result));
}

/**
 * @brief Calculates the floor of a division between two floating point numbers, treating a quotient within
 *        1e-9 of an integer as that integer
 * @param numerator The numerator
 * @param denominator The denominator
 * @return The rounded-down quotient
 */
inline long floor_division(const double numerator, const double denominator) {
    const double result = numerator / denominator;
    // if very close to an integer, use that integer instead of floor
    double rounded = std::round(result);
    if (std::abs(result - rounded) < 1e-9) {
        return static_cast<long>(rounded);
    }
    return static_cast<long>(std::floor(result));
}

/**
 * @brief Determines whether a value is strictly lower than another, with a relative tolerance of 1e-9
 *        (a plain comparison if either value is not finite)
 * @param candidate The candidate value
 * @param incumbent The value to compare it to
 * @return true if candidate < incumbent - 1e-9 * max(1, |candidate|, |incumbent|)
 */
inline bool is_strictly_better(double candidate, double incumbent) {

    if (!std::isfinite(candidate) || !std::isfinite(incumbent)) {
        return candidate < incumbent;
    }
    
    const double tol = 1e-9 * std::max({1.0, std::abs(candidate), std::abs(incumbent)});
    return candidate < incumbent - tol;
}

#endif //UTILS_H
