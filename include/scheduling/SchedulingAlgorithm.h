#ifndef SCHEDULINGALGORITHM_H
#define SCHEDULINGALGORITHM_H

#include <map>
#include <string>
#include <functional>
#include <memory>
#include <utility>

#include "ProbabilityComputation.h"
#include "JobTracker.h"
#include "OptionComparatorFunction.h"

namespace wrench {
    class JobTracker;

    class SchedulingAlgorithm {
    public:
        virtual ~SchedulingAlgorithm() = default;

        SchedulingAlgorithm(const double e_fail, std::string delta_t_scheme, const double delta_t_parameter,
                            const double restart_overhead, const double io_read_bandwidth,
                            const double io_write_bandwidth,
                            std::string name) : _e_fail(e_fail),
                                                _delta_t(-1),
                                                _delta_t_scheme(std::move(delta_t_scheme)),
                                                _delta_t_parameter(delta_t_parameter),
                                                _compute_always(delta_t_scheme == "compute_always"),
                                                _restart_overhead(restart_overhead),
                                                _io_read_bandwidth(io_read_bandwidth),
                                                _io_write_bandwidth(io_write_bandwidth),
                                                _name(std::move(name)) {
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
            const std::string& task_to_schedule,
            double input_data_size,
            double input_error_level,
            double remaining_time,
            OptionComparatorFunction* comparator_function,
            bool minimize) = 0;


        static std::shared_ptr<SchedulingAlgorithm> create_scheduling_algorithm(
            const std::string& type, double e_fail, std::string delta_t_scheme, double delta_t_parameter,
            double restart_overhead, double io_read_bandwidth, double io_write_bandwidth);

        std::string get_name() { return _name; }

    protected:
        double _e_fail;
        double _delta_t;
        std::string _delta_t_scheme;
        double _delta_t_parameter;
        bool _compute_always;
        double _restart_overhead;
        double _io_read_bandwidth;
        double _io_write_bandwidth;
        std::string _name;
    };
}

#endif //SCHEDULINGALGORITHM_H
