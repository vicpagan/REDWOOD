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
    std::shared_ptr<SchedulingAlgorithm> SchedulingAlgorithm::create_scheduling_algorithm(const std::string &type,
        const std::shared_ptr<ApplicationSpecs>& application_specs,
        const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
        ProbabilityComputation *probability_computation,
        OptionComparatorFunction* comparator_function) {
        if (type == "dynamic") {
            return std::make_shared<SchedulingAlgorithmDynamic>(application_specs, exec_options, probability_computation, comparator_function);
        } else if (type == "static") {
            return std::make_shared<SchedulingAlgorithmStatic>(application_specs, exec_options, probability_computation, comparator_function);
        } else {
            throw std::invalid_argument("Unknown scheduling algorithm '" + type + "'");
        }
    }

}
