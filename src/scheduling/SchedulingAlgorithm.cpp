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
    * @param e_fail: the failure error level
    * @param delta_t: the delta_t
    * @param delta_t_precision: the precision used for computing delta_t
    * @param restart_overhead: the restart overhead
    * @param io_read_bandwidth: the I/O read bandwidth
    * @param io_write_bandwidth: the I/O write bandwidth
    */
    std::shared_ptr<SchedulingAlgorithm> SchedulingAlgorithm::create_scheduling_algorithm(
        const std::string &type, double e_fail, std::string delta_t_scheme, double delta_t_parameter,
        const double restart_overhead, const double io_read_bandwidth, const double io_write_bandwidth) {
        if (type == "dynamic") {
            return std::make_shared<SchedulingAlgorithmDynamic>(e_fail, delta_t_scheme, delta_t_parameter,
                restart_overhead, io_read_bandwidth, io_write_bandwidth);
        } else if (type == "static") {
                return std::make_shared<SchedulingAlgorithmStatic>(e_fail, delta_t_scheme, delta_t_parameter,
                    restart_overhead, io_read_bandwidth, io_write_bandwidth);
        } else {
            throw std::invalid_argument("Unknown scheduling algorithm '" + type + "'");
        }
    }

}
