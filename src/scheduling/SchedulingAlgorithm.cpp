#include <iostream>
#include <limits>
#include <cmath>

#include "scheduling/SchedulingAlgorithm.h"
#include "ProbabilityComputation.h"
#include "scheduling/SchedulingAlgorithmDynamic.h"
#include "scheduling/SchedulingAlgorithmStatic.h"
#include "scheduling/SchedulingAlgorithmRandom.h"


namespace wrench {
    /**
    * @brief Instantiate a scheduling algorithm given its type/name
    * @param type: the algorithm's type/name
    * @param application_specs
    */
    std::shared_ptr<SchedulingAlgorithm> SchedulingAlgorithm::create_scheduling_algorithm(const std::string &type,
        const std::shared_ptr<ApplicationSpecs>& application_specs,
        const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
        ProbabilityComputation *probability_computation) {
        if (type == "dynamic") {
            return std::make_shared<SchedulingAlgorithmDynamic>(application_specs, exec_options, probability_computation);
        } else if (type.rfind("static_", 0) == 0) {  // starts with "static_"
            std::string comparator_name = type.substr(7);  // remove "static_"

            OptionComparatorFunction* chosen_comparator = nullptr;

            if (comparator_name == "expected_error") {
                chosen_comparator = new ExpectedErrorComparator(application_specs);
            }
            else if (comparator_name == "probability_success") {
                chosen_comparator = new ProbabilitySuccessComparator(application_specs);
            }
            else if (comparator_name == "error_level") {
                chosen_comparator = new ErrorLevelComparator(application_specs);
            }
            else if (comparator_name == "success_error_ratio") {
                chosen_comparator = new SuccessErrorRatioComparator(application_specs);
            }
            else {
                throw std::invalid_argument(
                    "Unknown static comparator '" + comparator_name + "'");
            }

            return std::make_shared<SchedulingAlgorithmStatic>(
                application_specs,
                exec_options,
                probability_computation,
                chosen_comparator
            );
        } else if (type == "random")  {
            return std::make_shared<SchedulingAlgorithmRandom>(application_specs, exec_options, probability_computation);
        } else {
            throw std::invalid_argument("Unknown scheduling algorithm '" + type + "'");
        }
    }

}
