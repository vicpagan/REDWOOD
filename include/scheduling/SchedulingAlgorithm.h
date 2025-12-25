#ifndef SCHEDULINGALGORITHM_H
#define SCHEDULINGALGORITHM_H

#include <map>
#include <string>
#include <functional>
#include <memory>
#include <utility>

#include "ApplicationSpecs.h"
#include "ProbabilityComputation.h"
#include "SystemState.h"
#include "OptionComparatorFunction.h"

namespace wrench {
    class SystemState;

    class SchedulingAlgorithm {
    public:
        virtual ~SchedulingAlgorithm() = default;

        SchedulingAlgorithm(const std::shared_ptr<ApplicationSpecs> &application_specs, std::string name) :
            _application_specs(application_specs),
            _e_fail(application_specs->get_e_fail()),
            _delta_t_scheme(application_specs->get_delta_t_scheme()),
            _delta_t_parameter(application_specs->get_delta_t_parameter()),
            _compute_always(application_specs->get_delta_t_scheme() == "compute_always"),
            _restart_overhead(application_specs->get_restart_overhead()),
            _io_read_bandwidth(application_specs->get_io_read_bandwidth()),
            _io_write_bandwidth(application_specs->get_io_write_bandwidth()),
            _delta_t(-1),
            _name(std::move(name)) {
        };

        struct SchedulingDecision {
            std::string hostname;
            std::string task;
            std::string execution_option;
        };

        virtual double get_optimal_expected_error() const = 0;

        virtual void preprocess_decisions(ProbabilityComputation* probability_computation,
            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
            double input_data_size,
            double input_error_level,
            double remaining_time) = 0;

        virtual std::vector<SchedulingDecision> make_decisions(
            SystemState* job_tracker,
            ProbabilityComputation* probability_computation,
            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
            const std::string& task_to_schedule,
            double input_data_size,
            double input_error_level,
            double remaining_time,
            OptionComparatorFunction* comparator_function,
            bool minimize) = 0;


        static std::shared_ptr<SchedulingAlgorithm> create_scheduling_algorithm(
            const std::string& type, const std::shared_ptr<ApplicationSpecs>& application_specs);

        std::string get_name() { return _name; }

        void reset_preprocessed_decisions() {
            _preprocessed_decisions.clear();
        }

    protected:
        std::shared_ptr<ApplicationSpecs> _application_specs;
        double _e_fail;
        std::string _delta_t_scheme;
        double _delta_t_parameter;
        bool _compute_always;
        double _restart_overhead;
        double _io_read_bandwidth;
        double _io_write_bandwidth;
        double _delta_t;
        std::string _name;

        std::map<std::string, std::vector<std::pair<std::string, double>>> _preprocessed_decisions;
    };
}

#endif //SCHEDULINGALGORITHM_H
