#ifndef SCHEDULINGALGORITHM_H
#define SCHEDULINGALGORITHM_H

#include <map>
#include <string>
#include <functional>
#include <memory>
#include "ProbabilityComputation.h"
#include "JobTracker.h"

namespace wrench {

    class JobTracker;

    class SchedulingAlgorithm {

    public:
        virtual ~SchedulingAlgorithm() = default;

        SchedulingAlgorithm(const double e_fail, const double delta_t, const double delta_t_precision,
                            const std::string& name) : _e_fail(e_fail),
                                                       _delta_t(delta_t), _delta_t_precision(delta_t_precision),
                                                       _name(name) {
        };

        struct SchedulingDecision {
            std::string hostname;
            std::string task;
            std::string execution_option;
        };

        virtual std::vector<SchedulingDecision> make_decisions(
            JobTracker* job_tracker,
            ProbabilityComputation* probability_computation,
            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
            const std::string &task_to_schedule,
            double input_data_size,
            double input_error_level,
            double remaining_time,
            double restart_overhead,
            double io_read_bandwidth,
            double io_write_bandwidth) = 0;


        static std::shared_ptr<SchedulingAlgorithm> create_scheduling_algorithm(
            const std::string& type, double e_fail, double delta_t, double delta_t_precision);

        std::string get_name() { return _name; }

    protected:
        double calculate_expected_error(std::vector<double>& dp,
                                        double exec_option_error,
                                        double probability_success,
                                        const std::vector<double>& probability_failures,
                                        long m_j,
                                        long n,
                                        long R) const;

        double _e_fail;
        double _delta_t;
        double _delta_t_precision;
        std::string _name;
    };
}

#endif //SCHEDULINGALGORITHM_H
