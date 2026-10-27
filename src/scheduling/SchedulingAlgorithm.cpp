#include <algorithm>
#include <iostream>
#include <limits>

#include "scheduling/SchedulingAlgorithm.h"
#include "ProbabilityComputation.h"
#include "scheduling/SchedulingAlgorithmDynamic.h"
#include "scheduling/SchedulingAlgorithmStaticForesighted.h"
#include "scheduling/SchedulingAlgorithmStaticNearsighted.h"
#include "scheduling/SchedulingAlgorithmGreedyForesighted.h"
#include "scheduling/SchedulingAlgorithmGreedyNearsighted.h"
#include "scheduling/SchedulingAlgorithmRandom.h"
#include "scheduling/HeuristicPlanning.h"
#include "OptimalExpectedError.h"
#include "DeltaTComputation.h"


namespace wrench {
    /**
    * @brief Instantiate a scheduling algorithm given its type/name
    * @param type: the algorithm's type/name
    * @param application_specs
    */
    std::shared_ptr<SchedulingAlgorithm> SchedulingAlgorithm::create_scheduling_algorithm(const std::string &type,
        const std::shared_ptr<ApplicationSpecs> &application_specs,
        const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double
            (double, double)> > > > &exec_options,
        ProbabilityComputation *probability_computation) {
        if (type == "dynamic_lower" || type == "dynamic_upper" || type == "dynamic") {
            // The decision table is computed for the lower or the upper bound on the expected error.
            // "dynamic" is kept for existing configurations and scripts: it is "dynamic_lower".
            const bool lower_bound = (type != "dynamic_upper");
            return std::make_shared<SchedulingAlgorithmDynamic>(application_specs, type, exec_options,
                                                                probability_computation, lower_bound);
        } else if (type.rfind("static_", 0) == 0) {
            // starts with "static_"
            std::string remainder = type.substr(7); // remove "static_"

            std::string variant;
            if (remainder.rfind("foresighted_", 0) == 0) {
                variant = "foresighted";
            } else if (remainder.rfind("nearsighted_", 0) == 0) {
                variant = "nearsighted";
            } else {
                throw std::invalid_argument(
                    "Invalid static algorithm format. Expected: static_<foresighted|nearsighted>_<criterion>");
            }

            const Criterion criterion = parse_criterion(remainder.substr(12));

            // Create appropriate static variant
            if (variant == "foresighted") {
                return std::make_shared<SchedulingAlgorithmStaticForesighted>(
                    application_specs, exec_options, probability_computation, criterion);
            } else if (variant == "nearsighted") {
                return std::make_shared<SchedulingAlgorithmStaticNearsighted>(
                    application_specs, exec_options, probability_computation, criterion);
            } else {
                throw std::invalid_argument("Unknown static variant '" + variant + "'");
            }
        } else if (type.rfind("greedy_", 0) == 0) {
            // starts with "greedy_"
            std::string remainder_after_type = type.substr(7); // remove "greedy_"

            std::string variant;
            if (remainder_after_type.rfind("foresighted_", 0) == 0) {
                variant = "foresighted";
            } else if (remainder_after_type.rfind("nearsighted_", 0) == 0) {
                variant = "nearsighted";
            } else {
                throw std::invalid_argument(
                    "Invalid greedy algorithm format. Expected: greedy_<foresighted|nearsighted>_<criterion>");
            }

            const Criterion criterion = parse_criterion(remainder_after_type.substr(12)); // remove "foresighted_" or "nearsighted_"

            // Create appropriate greedy variant
            if (variant == "foresighted") {
                return std::make_shared<SchedulingAlgorithmGreedyForesighted>(
                    application_specs, exec_options, probability_computation, criterion);
            } else if (variant == "nearsighted") {
                return std::make_shared<SchedulingAlgorithmGreedyNearsighted>(
                    application_specs, exec_options, probability_computation, criterion);
            } else {
                throw std::invalid_argument("Unknown greedy variant '" + variant + "'");
            }
        } else if (type == "random") {
            return std::make_shared<
                SchedulingAlgorithmRandom>(application_specs, exec_options, probability_computation);
        } else {
            throw std::invalid_argument("Unknown scheduling algorithm '" + type + "'");
        }
    }
}
