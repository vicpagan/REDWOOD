#include <iostream>
#include <limits>
#include <cmath>

#include "scheduling/SchedulingAlgorithm.h"
#include "ProbabilityComputation.h"
#include "scheduling/SchedulingAlgorithmDynamic.h"
#include "scheduling/SchedulingAlgorithmStatic.h"


namespace wrench {
    /**
    * @brief Instantiate a scheduling algorithm given its type/name
    * @param type: the algorithm's type/name
    * @param application_specs
    */
    std::shared_ptr<SchedulingAlgorithm> SchedulingAlgorithm::create_scheduling_algorithm(
        const std::string &type, const std::shared_ptr<ApplicationSpecs>& application_specs) {
        if (type == "dynamic") {
            return std::make_shared<SchedulingAlgorithmDynamic>(application_specs);
        } else if (type == "static") {
            return std::make_shared<SchedulingAlgorithmStatic>(application_specs);
        } else {
            throw std::invalid_argument("Unknown scheduling algorithm '" + type + "'");
        }
    }

}
