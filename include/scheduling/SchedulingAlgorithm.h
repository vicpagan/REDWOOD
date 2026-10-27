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

namespace wrench {
    class SystemState;

    class SchedulingAlgorithm {
    public:
        virtual ~SchedulingAlgorithm() = default;

        SchedulingAlgorithm(const std::shared_ptr<ApplicationSpecs> &application_specs,
                            std::string name,
                            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double
                                (double, double)> > > > &exec_options,
                            ProbabilityComputation *probability_computation) : _application_specs(application_specs),
                                                                               _exec_options(exec_options),
                                                                               _probability_computation(
                                                                                   probability_computation),
                                                                               _e_fail(application_specs->get_e_fail()),
                                                                               _io_read_bandwidth_per_node(
                                                                                   application_specs->
                                                                                   get_io_read_bandwidth_per_node()),
                                                                               _io_write_bandwidth_per_node(
                                                                                   application_specs->
                                                                                   get_io_write_bandwidth_per_node()),
                                                                               _name(std::move(name)) {
        };

        struct SchedulingDecision {
            std::string hostname;
            std::string task;
            std::string execution_option;
        };

        virtual void preprocess_host_decisions(
            const std::string &hostname,
            double initial_data_size,
            double initial_error_level,
            double deadline,
            bool lower_bound) = 0;

        virtual std::vector<SchedulingDecision> make_decisions(
            SystemState *system_state_tracker,
            double remaining_time) = 0;


        static std::shared_ptr<SchedulingAlgorithm> create_scheduling_algorithm(
            const std::string &type,
            const std::shared_ptr<ApplicationSpecs> &application_specs,
            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double
                (double, double)> > > > &exec_options,
            ProbabilityComputation *probability_computation);

        std::string get_name() { return _name; }


        virtual void reset_host_preprocessed_decisions(const std::string &hostname) = 0;

        virtual void reset_all_preprocessed_decisions() = 0;

    protected:
        std::shared_ptr<ApplicationSpecs> _application_specs;
        const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double
            (double, double)> > > > &_exec_options;

        ProbabilityComputation *_probability_computation;

        double _e_fail;
        double _io_read_bandwidth_per_node;
        double _io_write_bandwidth_per_node;
        std::string _name;
    };
}

#endif //SCHEDULINGALGORITHM_H
