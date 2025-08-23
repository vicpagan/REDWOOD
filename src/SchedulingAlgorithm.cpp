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
    */
    std::shared_ptr<SchedulingAlgorithm> SchedulingAlgorithm::create_scheduling_algorithm(
        const std::string &type, double e_fail, double delta_t, double delta_t_precision,
        const double restart_overhead, const double io_read_bandwidth, const double io_write_bandwidth) {
        if (type == "dynamic") {
            return std::make_shared<SchedulingAlgorithmDynamic>(e_fail, delta_t, delta_t_precision,
                restart_overhead, io_read_bandwidth, io_write_bandwidth);
        } else if (type == "static") {
                return std::make_shared<SchedulingAlgorithmStatic>(e_fail, delta_t, delta_t_precision,
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
    double SchedulingAlgorithm::calculate_expected_error(std::vector<double> &dp,
                                                         const double exec_option_error,
                                                         const double probability_success,
                                                         const std::vector<double> &probability_failures,
                                                         const long m_j,
                                                         const long n,
                                                         const long R) const {
        /* BASE CASE: If there are fewer than m_j time steps remaining, we fail */
        for (long i = 0; i < m_j && i <= n; i++) {
            dp[i] = _e_fail;
        }

        for (long k = m_j; k <= n; k++) {
            double expected_error = probability_success * exec_option_error;
            for (long u = 0; u < m_j; u++) {
                expected_error += probability_failures[u] * dp[std::max((k - u - R - 1), 0L)];
            }
            dp[k] = expected_error;
        }

        return dp[n];
    }

}
