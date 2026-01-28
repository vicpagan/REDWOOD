#ifndef UTILS_H
#define UTILS_H

#include <boost/json.hpp>

boost::json::object readJSONFromFile(const std::string& filepath);

/**
 * @brief Calculates the ceiling of a division between two floating point numbers
 * @param numerator
 * @param denominator
 * @return
 */
inline long ceiling_division(const double numerator, const double denominator) {
    const double result = numerator / denominator;
    // If very close to an integer, use that integer instead of ceiling
    double rounded = std::round(result);
    if (std::abs(result - rounded) < 1e-9) {
        return static_cast<long>(rounded);
    }
    return static_cast<long>(std::ceil(result));
}

#endif //UTILS_H
