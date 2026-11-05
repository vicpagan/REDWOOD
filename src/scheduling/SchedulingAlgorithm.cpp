#include <limits>
#include <cmath>

#include "scheduling/SchedulingAlgorithm.h"
#include "Discretization.h"
#include "scheduling/HeuristicPlanning.h"
#include "scheduling/SchedulingAlgorithmDynamic.h"
#include "scheduling/SchedulingAlgorithmStaticForesighted.h"
#include "scheduling/SchedulingAlgorithmStaticNearsighted.h"
#include "scheduling/SchedulingAlgorithmGreedyForesighted.h"
#include "scheduling/SchedulingAlgorithmGreedyNearsighted.h"
#include "scheduling/SchedulingAlgorithmRandom.h"


namespace wrench {
    /**
     * @brief Instantiate a scheduling algorithm given its name. Accepted names:
     *        "random",
     *        "dynamic_<lower|upper>",
     *        "static_<foresighted|nearsighted>_<criterion>_<lower|upper>",
     *        "greedy_<foresighted|nearsighted>_<criterion>_<lower|upper>",
     *        where <criterion> is expected_error, probability_success, success_error_ratio or error_level.
     *        Except for "random", the _lower/_upper suffix is required: it selects whether durations are
     *        discretized for the lower or the upper bound (Section V-B).
     * @param type The algorithm's name
     * @param application_specs The application specification
     * @param exec_options The option functions of each option of each task
     * @param discretization The discretization shared by all algorithms
     * @return The scheduling algorithm (its name is type)
     * @throws std::invalid_argument if the name is unknown or malformed
     */
    std::shared_ptr<SchedulingAlgorithm> SchedulingAlgorithm::create_scheduling_algorithm(const std::string &type,
                                                                                          const std::shared_ptr<ApplicationSpecs>& application_specs,
                                                                                          const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
                                                                                          Discretization *discretization) {
        if (type == "random") {
            return std::make_shared<SchedulingAlgorithmRandom>(application_specs, exec_options, discretization);
        }

        // remove the "_lower" or "_upper" suffix
        bool lower_bound;
        std::string base_type;
        if (type.size() > 6 && type.compare(type.size() - 6, 6, "_lower") == 0) {
            lower_bound = true;
            base_type = type.substr(0, type.size() - 6);
        }
        else if (type.size() > 6 && type.compare(type.size() - 6, 6, "_upper") == 0) {
            lower_bound = false;
            base_type = type.substr(0, type.size() - 6);
        }
        else {
            throw std::invalid_argument("Invalid scheduling algorithm '" + type +
                                        "': the name must end with _lower or _upper (e.g., dynamic_lower)");
        }

        if (base_type == "dynamic") {
            return std::make_shared<SchedulingAlgorithmDynamic>(application_specs, type, exec_options,
                                                                discretization, lower_bound);
        }
        else if (base_type.rfind("static_", 0) == 0) {  // starts with "static_"
            std::string remainder = base_type.substr(7);  // remove "static_"

            std::string variant;
            if (remainder.rfind("foresighted_", 0) == 0) {
                variant = "foresighted";
            }
            else if (remainder.rfind("nearsighted_", 0) == 0) {
                variant = "nearsighted";
            }
            else {
                throw std::invalid_argument("Invalid static algorithm format. Expected: static_<foresighted|nearsighted>_<criterion>_<lower|upper>");
            }

            std::string criterion = remainder.substr(12); // remove "foresighted_" or "nearsighted_"
            if (!is_valid_criterion(criterion)) {
                throw std::invalid_argument("Unknown static criterion '" + criterion + "'");
            }

            // create appropriate static variant
            if (variant == "foresighted") {
                return std::make_shared<SchedulingAlgorithmStaticForesighted>(
                    application_specs, type, exec_options, discretization, criterion, lower_bound);
            }
            else {
                return std::make_shared<SchedulingAlgorithmStaticNearsighted>(
                    application_specs, type, exec_options, discretization, criterion, lower_bound);
            }
        }
        else if (base_type.rfind("greedy_", 0) == 0) {  // starts with "greedy_"
            std::string remainder = base_type.substr(7);  // remove "greedy_"

            std::string variant;
            if (remainder.rfind("foresighted_", 0) == 0) {
                variant = "foresighted";
            }
            else if (remainder.rfind("nearsighted_", 0) == 0) {
                variant = "nearsighted";
            }
            else {
                throw std::invalid_argument("Invalid greedy algorithm format. Expected: greedy_<foresighted|nearsighted>_<criterion>_<lower|upper>");
            }

            std::string criterion = remainder.substr(12); // remove "foresighted_" or "nearsighted_"
            if (!is_valid_criterion(criterion)) {
                throw std::invalid_argument("Unknown greedy criterion '" + criterion + "'");
            }

            // create appropriate greedy variant
            if (variant == "foresighted") {
                return std::make_shared<SchedulingAlgorithmGreedyForesighted>(
                        application_specs, type, exec_options, discretization, criterion, lower_bound);
            }
            else {
                return std::make_shared<SchedulingAlgorithmGreedyNearsighted>(
                        application_specs, type, exec_options, discretization, criterion, lower_bound);
            }
        }
        else {
            throw std::invalid_argument("Unknown scheduling algorithm '" + type + "'");
        }
    }
}
