/**
 * Copyright (c) 2017-2018. The WRENCH Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */


#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <wrench-dev.h>
#include <boost/json/object.hpp>

#include "ProbabilityComputation.h"
#include "ApplicationSpecs.h"
#include "scheduling/SchedulingAlgorithm.h"

class SchedulingAlgorithm;

namespace wrench {
    class NodeKiller;
    class SystemState;

    /**
     *  @brief An Execution Controller implementation
     */
    class Controller : public ExecutionController {
    public:
        // Constructor
        Controller(
            const boost::json::object &application_spec,
            const boost::json::object &execution_spec,
            const boost::json::object &scheduling_spec,
            const std::shared_ptr<ApplicationSpecs> &_application_specs,
            const std::map<std::string, std::shared_ptr<BareMetalComputeService> > &compute_services,
            const bool fake_io,
            const std::string &hostname);

    private:
        /// State of the repetition in progress
        struct RepetitionState {
            int repeat; ///< repetition index
            double start_date; ///< simulated date at which the repetition started
            double deadline_date; ///< simulated date of the deadline
            /// Error level that each host's decision tree has been pruned to improve on (temporal redundancy)
            std::map<std::string, double> tree_prune_threshold_by_host;
        };

        int main() override;

        void run_repetition(const std::shared_ptr<SchedulingAlgorithm> &algorithm,
                            int repeat,
                            std::vector<std::pair<bool, double> > &repetition_results);

        bool process_job_completion(const std::shared_ptr<CompoundJobCompletedEvent> &success_event,
                                    const std::shared_ptr<SchedulingAlgorithm> &algorithm,
                                    RepetitionState &state,
                                    std::vector<std::pair<bool, double> > &repetition_results);

        bool process_timer_event(const std::shared_ptr<TimerEvent> &timer_event,
                                 const std::shared_ptr<SchedulingAlgorithm> &algorithm,
                                 const RepetitionState &state,
                                 std::vector<std::pair<bool, double> > &repetition_results);

        void apply_temporal_redundancy(const std::string &success_hostname,
                                       double final_error_level,
                                       const std::shared_ptr<SchedulingAlgorithm> &algorithm,
                                       RepetitionState &state);

        void restart_system() const;

        void submit_job(const std::string &task_name,
                        const std::string &execution_option,
                        double running_output_data_size,
                        double running_output_error_level,
                        const std::string &hostname);

        void terminate_running_job(const std::string &hostname, const std::string &context) const;

        void restart_host_from_first_task(const std::string &hostname,
                                          const std::shared_ptr<SchedulingAlgorithm> &algorithm,
                                          double prune_threshold,
                                          std::map<std::string, double> &tree_prune_threshold_by_host,
                                          bool terminate_job,
                                          const std::string &context);

        void continue_from_host_output(const std::string &hostname,
                                       const std::string &success_hostname,
                                       const std::shared_ptr<SchedulingAlgorithm> &algorithm,
                                       double prune_threshold,
                                       std::map<std::string, double> &tree_prune_threshold_by_host,
                                       const std::string &context);

        std::shared_ptr<ApplicationSpecs> _application_specs;

        std::unique_ptr<ProbabilityComputation> _probability_computation;

        const boost::json::object _application_spec;
        const boost::json::object _execution_spec;
        const boost::json::object _scheduling_spec;
        const std::map<std::string, std::shared_ptr<BareMetalComputeService> > _compute_services;
        const bool _fake_io;

        std::shared_ptr<SystemState> _system_state_tracker;
        std::shared_ptr<JobManager> _job_manager;

        long _num_repeats;
        std::string _temporal_redundancy;
        std::string _stop_running_jobs;
        bool _keep_preprocessed_decisions;

        const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double
            (double, double)> > > > &
        _task_functions;
        std::vector<std::pair<std::vector<std::string>, double> > _execution_combinations;
        std::vector<std::shared_ptr<wrench::SchedulingAlgorithm> > _scheduling_algorithms;
    };
} // namespace wrench
#endif//CONTROLLER_H
