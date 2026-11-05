#ifndef SCHEDULINGALGORITHM_H
#define SCHEDULINGALGORITHM_H

#include <map>
#include <string>
#include <functional>
#include <memory>
#include <utility>

#include "ApplicationSpecs.h"
#include "Discretization.h"
#include "SystemState.h"

namespace wrench {
    class SystemState;

    class SchedulingAlgorithm {
    public:
        virtual ~SchedulingAlgorithm() = default;

        /**
         * @brief Constructor
         * @param application_specs The application specification
         * @param name The algorithm's name (as given in the configuration)
         * @param exec_options The option functions (t_function, d_function, e_function) of each option of each task
         * @param discretization The discretization shared by all algorithms
         * @param lower_bound Whether durations are discretized for the lower bound (optimistic) on the
         *        expected error, instead of the upper bound (pessimistic) (Section V-B)
         */
        SchedulingAlgorithm(const std::shared_ptr<ApplicationSpecs> &application_specs,
            std::string name,
            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
            Discretization* discretization,
            const bool lower_bound) :

            _application_specs(application_specs),
            _exec_options(exec_options),
            _discretization(discretization),
            _e_fail(application_specs->get_e_fail()),
            _restart_overhead(application_specs->get_restart_overhead()),
            _io_read_bandwidth_per_node(application_specs->get_io_read_bandwidth_per_node()),
            _io_write_bandwidth_per_node(application_specs->get_io_write_bandwidth_per_node()),
            _lower_bound(lower_bound),
            _name(std::move(name)) {
        };

        struct SchedulingDecision {
            std::string hostname;
            std::string task;
            std::string execution_option;
        };

        /**
         * @brief Plan the decisions of a host from its current state (called when the host has no stored
         *        decision for its next task)
         * @param hostname The host
         * @param initial_data_size The input data size of the host's next task
         * @param initial_error_level The input error level of the host's next task
         * @param deadline The remaining time before the deadline
         * @param lower_bound Whether durations are discretized for the lower bound (instead of the upper bound)
         */
        virtual void preprocess_host_decisions(
            const std::string& hostname,
            double initial_data_size,
            double initial_error_level,
            double deadline,
            bool lower_bound) = 0;

        /**
         * @brief Choose an execution option for the next task of each idle host
         * @param system_state_tracker The state of the hosts
         * @param remaining_time The remaining time before the deadline
         * @return The decisions, one per idle host
         */
        virtual std::vector<SchedulingDecision> make_decisions(
            SystemState* system_state_tracker,
            double remaining_time) = 0;


        static std::shared_ptr<SchedulingAlgorithm> create_scheduling_algorithm(
            const std::string& type,
            const std::shared_ptr<ApplicationSpecs>& application_specs,
            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
            Discretization* discretization);

        /**
         * @brief Get the algorithm's name
         * @return The name
         */
        std::string get_name() { return _name; }

        /**
         * @brief Discard the stored decisions of a host, so that it is planned again
         * @param hostname The host
         */
        virtual void reset_host_preprocessed_decisions(const std::string& hostname) = 0;

        /**
         * @brief Discard the stored decisions of all hosts
         */
        virtual void reset_all_preprocessed_decisions() = 0;

    protected:
        std::shared_ptr<ApplicationSpecs> _application_specs;
        const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>> &_exec_options;

        Discretization* _discretization;

        double _e_fail;
        double _restart_overhead;
        double _io_read_bandwidth_per_node;
        double _io_write_bandwidth_per_node;
        bool _lower_bound;
        std::string _name;

        std::vector<std::string> _completed_execution_path;
    };
}

#endif //SCHEDULINGALGORITHM_H
