#include <iostream>
#include <limits>
#include <cmath>

#include "SchedulingAlgorithm.h"
#include "ProbabilityComputation.h"
#include "SchedulingAlgorithmDynamic.h"
#include "SchedulingAlgorithmStatic.h"


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

    /**
     * @brief Calculates expected error recursively for one execution option for a single task
     *
     * @param dp Dynamic programming array
     * @param exec_option_error This is our e(x, y) for this execution option
     * @param probability_failures This is the list of our p_us
     * @param probability_success This is our e^(-lambda * m_j * delta)
     * @param m_j This is m_j in the paper
     * @param n This is n in the paper
     * @param R this is R in the paper
     * @return The expected error for the selected execution option
     */
    double SchedulingAlgorithm::calculate_expected_error(std::map<std::string, double> &exec_option_errors,
        std::map<std::string, long> &m_j,
        long n,
        long R,
        std::map<std::string, double> &probability_success,
        std::map<std::string, std::vector<double>> &probability_failures) const {
        for (const auto& [option_name, option_error_level] : exec_option_errors) {
            double expected_error = probability_success.at(option_name) * option_error_level;
            for (long u = 0; u < m_j.at(option_name); u++) {
                expected_error += probability_failures.at(option_name).at(u) * calculate_expected_error(std::max((n - u - R - 1), 0L));
            }
        }
    }

}
