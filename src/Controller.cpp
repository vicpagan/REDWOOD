/**
 * Copyright (c) 2017-2021. The WRENCH Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

/**
 ** An execution controller to execute a workflow
 **/

#define GFLOP (1000.0 * 1000.0 * 1000.0)
#define MBYTE (1000.0 * 1000.0)
#define GBYTE (1000.0 * 1000.0 * 1000.0)

#ifndef OPTIMISTIC_DISCRETIZATION
#define OPTIMISTIC_DISCRETIZATION 0
#endif

#include <iostream>
#include <wrench/util/UnitParser.h>

#include "Controller.h"
#include "FunctionGenerator.h"
#include "NodeKiller.h"
#include "ProbabilityComputation.h"
#include "SystemState.h"
#include "scheduling/SchedulingAlgorithm.h"

WRENCH_LOG_CATEGORY(controller, "Log category for Controller");

namespace wrench {
    /**
     * @brief Constructor
     *
     * @param platform_spec: platform specifications
     * @param failure_spec: failure specifications
     * @param application_spec: application specifications
     * @param execution_spec: application specifications
     * @param scheduling_spec: scheduling specifications
     * @param compute_services: a set of compute services available to run actions
     * @param hostname: the name of the host on which to start the Execution Controller
     */
    Controller::Controller(const boost::json::object& application_spec,
                           const boost::json::object& execution_spec,
                           const boost::json::object& scheduling_spec,
                           const std::shared_ptr<ApplicationSpecs>& application_specs,
                           const std::map<std::string, std::shared_ptr<BareMetalComputeService>>& compute_services,
                           const std::string& hostname) : ExecutionController(hostname, "controller"),
                                                          _application_specs(application_specs),
                                                          _application_spec(application_spec),
                                                          _execution_spec(execution_spec),
                                                          _scheduling_spec(scheduling_spec),
                                                          _compute_services(compute_services),
                                                          _task_functions(_application_specs->get_task_functions()) {

        _num_repeats = boost::json::value_to<long>(_execution_spec.at("num_repeats"));
        _temporal_redundancy = boost::json::value_to<std::string>(
            _scheduling_spec.at("hacks").as_object().at("temporal_redundancy"));
        _stop_running_jobs = boost::json::value_to<std::string>(
            _scheduling_spec.at("hacks").as_object().at("stop_running_jobs"));

        /* Create the probability computation utility */
        _probability_computation = std::make_unique<ProbabilityComputation>(_application_specs);

        /* Determine the list of scheduling algorithms */
        /* Determine/validate the scheduling algorithms to use */
        for (auto const &alg_name: _scheduling_spec.at("algorithms").as_array()) {
            auto alg = SchedulingAlgorithm::create_scheduling_algorithm(
                boost::json::value_to<string>(alg_name), _application_specs, _task_functions,
                _probability_computation.get());

            _scheduling_algorithms.push_back(alg);
        }
    }

    /**
     * @brief Helper function to create and submit a job
     * @param task_name The task name
     * @param execution_option The execution option
     * @param running_output_data_size The running data size
     * @param running_output_error_level The running error
     * @param hostname The hostname on which to start the job
     * @return a job
     */
    void Controller::submit_job(const std::string& task_name,
                                const std::string& execution_option,
                                double running_output_data_size,
                                double running_output_error_level,
                                const std::string& hostname) {
        const auto job = _job_manager->createCompoundJob(task_name + "_" + execution_option);

        // std::cerr << "Submitting job for task " << task_name << " with execution option " << execution_option <<
        //     " on host " << hostname << std::endl;

        const auto read_input_action = job->addCustomAction("read", 0, 1,
                                                      [this, running_output_data_size](
                                                      const std::shared_ptr<wrench::ActionExecutor>& action_executor) {
                                                          auto disk = wrench::S4U_Simulation::hostHasMountPoint(action_executor->hostname, "/");
                                                          disk->write(
                                                              static_cast<sg_size_t>(running_output_data_size));
                                                      },
                                                      [](const std::shared_ptr<wrench::ActionExecutor>&
                                                      action_executor) {
                                                      });

        // std::cerr << "Read input action for task " << task_name << " with execution option " << execution_option <<
        //     " on host " << hostname << std::endl;

        const auto compute_action = job->addComputeAction("compute",
                                                    _task_functions.at(task_name).at(execution_option).at("t_function")
                                                    (running_output_data_size, running_output_error_level),
                                                    0.0,
                                                    1, 1, ParallelModel::CONSTANTEFFICIENCY(1.0));

        // std::cerr << "Compute action for task " << task_name << " with execution option " << execution_option <<
        //     " on host " << hostname << std::endl;

        const auto write_output_action = job->addCustomAction("write", 0, 1,
                                                        [this, task_name, execution_option, running_output_data_size,
                                                            running_output_error_level](
                                                        const std::shared_ptr<wrench::ActionExecutor>&
                                                        action_executor) {
                                                            auto disk = wrench::S4U_Simulation::hostHasMountPoint(action_executor->hostname, "/");
                                                            disk->write(static_cast<sg_size_t>(
                                                                _task_functions.at(task_name).at(execution_option).at("d_function")
                                                                (running_output_data_size,
                                                                 running_output_error_level)));
                                                        },
                                                        [](const std::shared_ptr<wrench::ActionExecutor>&
                                                        action_executor) {
                                                        });

        // std::cerr << "Write output action for task " << task_name << " with execution option " << execution_option <<
        //     " on host " << hostname << std::endl;

        job->addActionDependency(read_input_action, compute_action);
        job->addActionDependency(compute_action, write_output_action);

        WRENCH_INFO("Submitting a new job to %s", hostname.c_str());
        _job_manager->submitJob(job, _compute_services.at(hostname));

        _system_state_tracker->track_job(job, hostname, task_name, execution_option, Simulation::getCurrentSimulatedDate());
    }

    void Controller::restart_system() const {
        for (int i = 0; i < _application_specs->get_num_compute_nodes(); i++) {
            std::string hostname = "ComputeHost_" + std::to_string(i);
            // std::cout << "Resetting host " << hostname << std::endl;
            if (_system_state_tracker->is_a_job_running(hostname)) {
                // std::cout << "Job is running on " << hostname << ", terminating it" << std::endl;
                try {
                    _job_manager->terminateJob(
                        _system_state_tracker->get_running_job(hostname));
                }
                catch (ExecutionException&) {
                    // std::cerr << "Tried to terminate job on down host: " << hostname << " for stop running jobs hack" << std::endl;
                }
                _system_state_tracker->reset_host(hostname);
                _system_state_tracker->untrack_job(hostname);
                _system_state_tracker->set_host_up(hostname);
            }
        }
    }

    /**
     * @brief main method of the Controller
     *
     * @return 0 on completion
     *
     */
    int Controller::main() {
        TerminalOutput::setThisProcessLoggingColor(TerminalOutput::COLOR_GREEN);
        WRENCH_INFO("Controller starting");

        // std::cerr << "OPTIMISTIC DISCRETIZATION: " << OPTIMISTIC_DISCRETIZATION << std::endl;

        /* Create a job manager so that we can create/submit jobs */
        _job_manager = this->createJobManager();

        /* Create the system state tracker */
        std::vector<std::string> hostnames;
        for (auto const &entry : _compute_services) { hostnames.push_back(entry.first); }
        _system_state_tracker = SystemState::create_tracker(hostnames);

        /* Get initial x (data size) and y (error) from the JSON file */
        auto initial_data_size = boost::json::value_to<double>(_application_spec.at("initial_data_size"));
        auto initial_error_level = boost::json::value_to<double>(_application_spec.at("initial_error_level"));

        /* Loop over all the scheduling algorithms */
        for (const auto& algorithm : _scheduling_algorithms) {
            std::cerr << "** " << algorithm->get_name().c_str() << " **" << std::endl;
            WRENCH_INFO("** Running experiments with algorithm '%s' **", algorithm->get_name().c_str());

            /* Keep track of number of successes */
            std::vector<std::pair<bool, double>> repetition_results(_num_repeats, {false, _application_specs->get_e_fail()});

            /* Do all the repeats */
            for (int repeat = 0; repeat < _num_repeats; repeat++) {
                double repeat_start_date = Simulation::getCurrentSimulatedDate();
                std::cout << "Repetition " << repeat << std::endl;

                /* (Re-)Create node on/off turners, resetting the seed at every experiment start */
                NodeKiller::start_node_killers(this->getSimulation(),
                                               _compute_services,
                                               _application_specs->get_seed(),
                                               (repeat == 0),
                                               _application_specs->get_exponential_distribution(),
                                               _application_specs->get_restart_overhead(),
                                               this->commport);

                /* Create an alarm for the deadline */
                auto time_to_deadline = Simulation::getCurrentSimulatedDate() + _application_specs->get_deadline();
                // WRENCH_INFO("Setting an alarm for repeat %d at time %lf", repeat, execution_deadline);
                this->setTimer(time_to_deadline, "time_out:" + algorithm->get_name() + "-" + std::to_string(repeat));

                /* Running values of output data size, error level, and the best error level for each host */
                std::map<std::string, double> best_error_level_by_host;
                for (auto &entry : _compute_services) {
                    best_error_level_by_host[entry.first] = _application_specs->get_e_fail();
                }

                /* Current task for all hosts is the first task */
                for (auto const& entry : _compute_services) {
                    _application_specs->update_host_task_to_schedule(entry.first, 0);
                    _application_specs->update_host_running_data_size(entry.first, initial_data_size);
                    _application_specs->update_host_running_error_level(entry.first, initial_error_level);
                }

                /* Build/reset decision trees to use for temporal redundancy */
                if (repeat == 0) {
                    _application_specs->clear_decision_trees();
                    _application_specs->build_decision_trees();
                }
                _application_specs->reset_all_hosts_current_decision_nodes();
                _application_specs->reset_all_hosts_decision_history();
                // algorithm->reset_all_preprocessed_decisions();

                _system_state_tracker->reset_all_hosts();

                /* Loop until an event message arrives */
                while (true) {
                    // Invoke the scheduler
                    auto decisions =
                        algorithm->make_decisions(_system_state_tracker.get(),
                           time_to_deadline - Simulation::getCurrentSimulatedDate());

                    // Implement the scheduling decisions
                    for (const auto &[hostname, task, execution_option] : decisions) {
                        // std::cerr << "Scheduling decision: run task " << task <<
                        //     " with option " << execution_option <<
                        //     " on host " << hostname << " at time " << Simulation::getCurrentSimulatedDate()  - repeat_start_date <<std::endl;
                        this->submit_job(task,
                                         execution_option,
                                         _application_specs->get_host_running_data_size(hostname),
                                         _application_specs->get_host_running_error_level(hostname),
                                         hostname);
                    }

                    // Here we could instead call waitForAndProcessNextEvent() and define the handling
                    // methods, in case this if-else-if thing becomes too unwieldly
                    auto event = this->waitForNextEvent();
                    if (auto success_event = std::dynamic_pointer_cast<CompoundJobCompletedEvent>(event)) {
                        auto success_hostname = success_event->compute_service->getHosts().at(0);

                        auto job_task_name = success_event->job->getName();
                        auto success_host_completed_task_name = _system_state_tracker->get_host_current_task(success_hostname);

                        // checks if the task completed in the event is the same as the current task the host is working on
                        // if its different, we know the host in question has already been reset and rescheduled
                        // if its the same, then we have to reset the host
                        if (job_task_name.compare(0, success_host_completed_task_name.length(), success_host_completed_task_name)) {
                            // spurious timeout or whatever its called
                            continue;
                        }

                        // checks if the task completed in the event is the same as the current task the host SHOULD have been working on
                        // if its different, then we have to reset the host to make sure it gets rescheduled to the proper task
                        // if its the same, then we know it completed what it set out to do successfully
                        std::string success_host_current_task_to_schedule = _application_specs->get_host_task_to_schedule(success_hostname);
                        if (job_task_name.compare(0, success_host_current_task_to_schedule.length(), success_host_current_task_to_schedule)) {
                            _system_state_tracker->reset_host(success_hostname);
                            _system_state_tracker->untrack_job(success_hostname);
                            continue;
                        }
                        std::cout << job_task_name << " completed successfully by host " << success_hostname << " at " << (Simulation::getCurrentSimulatedDate() - repeat_start_date) << std::endl;

                        std::string success_host_selected_option = job_task_name.substr(success_host_completed_task_name.length() + 1);
                        double success_host_running_data_size = _application_specs->get_host_running_data_size(success_hostname);
                        double success_host_running_error_level = _application_specs->get_host_running_error_level(success_hostname);

                        // update data size and error level trackers
                        std::cerr << "success_host_completed_task_name = " << success_host_completed_task_name << "\n";
                        std::cerr << "Completed task name: " << success_host_completed_task_name << std::endl;
                        std::cerr << "Completed selected option: " << success_host_selected_option << std::endl;
                        _application_specs->update_host_running_data_size(success_hostname, _task_functions.at(success_host_completed_task_name).at(success_host_selected_option).at("d_function")(success_host_running_data_size, success_host_running_error_level));
                        _application_specs->update_host_running_error_level(success_hostname, _task_functions.at(success_host_completed_task_name).at(success_host_selected_option).at("e_function")(success_host_running_data_size, success_host_running_error_level));
                        _application_specs->increment_host_task_to_schedule(success_hostname);
                        _application_specs->increment_host_current_decision_node(success_hostname, success_host_completed_task_name, success_host_selected_option);

                        // update local trackers
                        success_host_current_task_to_schedule = _application_specs->get_host_task_to_schedule(success_hostname);
                        success_host_running_data_size = _application_specs->get_host_running_data_size(success_hostname);
                        success_host_running_error_level = _application_specs->get_host_running_error_level(success_hostname);

                        if (_stop_running_jobs == "aggressive") {
                            for (const auto& entry : _compute_services) {
                                std::string hostname = entry.first;
                                if (hostname != success_hostname && !_system_state_tracker->is_host_finished(hostname)) {
                                    algorithm->reset_host_preprocessed_decisions(hostname);

                                    _application_specs->update_host_running_data_size(hostname, success_host_running_data_size);
                                    _application_specs->update_host_running_error_level(hostname, success_host_running_error_level);
                                    _application_specs->update_host_task_to_schedule(hostname, success_host_current_task_to_schedule);

                                    _application_specs->update_host_decision_history(hostname, success_hostname);
                                    _application_specs->clear_decision_tree(hostname);
                                    _application_specs->build_decision_tree(hostname);
                                    _application_specs->prune_decision_tree(hostname, best_error_level_by_host.at(success_hostname));
                                    _application_specs->update_host_current_decision_node(hostname, success_hostname);

                                    if (_system_state_tracker->is_a_job_running(hostname)) {
                                        try {
                                            _job_manager->terminateJob(
                                                _system_state_tracker->get_running_job(hostname));
                                        }
                                        catch (ExecutionException&) {
                                            std::cerr << "Tried to terminate job on host: " << hostname << " with SRJ AGG" << std::endl;
                                        }
                                    }
                                    _system_state_tracker->untrack_job(hostname);
                                    if (!_system_state_tracker->is_host_down(hostname)) {
                                        NodeKiller::reset_node_killer(
                                            this->getSimulation(),
                                            hostname,
                                            _application_specs->get_exponential_distribution(),
                                            _application_specs->get_restart_overhead(),
                                            this->commport);
                                        _system_state_tracker->reset_host(hostname);
                                    }
                                    else {
                                        _system_state_tracker->reset_host(hostname);
                                        _system_state_tracker->set_host_down(hostname);
                                    }
                                    // _system_state_tracker->untrack_job(hostname);
                                }
                            }
                        } else if (_stop_running_jobs == "variant") {
                            for (const auto& entry : _compute_services) {
                                std::string hostname = entry.first;
                                if (hostname != success_hostname) {
                                    if (!_application_specs->can_possibly_do_better(hostname, success_hostname) && !_system_state_tracker->is_host_finished(hostname)) {
                                        algorithm->reset_host_preprocessed_decisions(hostname);

                                        _application_specs->update_host_running_data_size(hostname, success_host_running_data_size);
                                        _application_specs->update_host_running_error_level(hostname, success_host_running_error_level);
                                        _application_specs->update_host_task_to_schedule(hostname, success_host_current_task_to_schedule);

                                        _application_specs->update_host_decision_history(hostname, success_hostname);
                                        _application_specs->clear_decision_tree(hostname);
                                        _application_specs->build_decision_tree(hostname);
                                        _application_specs->prune_decision_tree(hostname, best_error_level_by_host.at(success_hostname));
                                        _application_specs->update_host_current_decision_node(hostname, success_hostname);

                                        if (_system_state_tracker->is_a_job_running(hostname)) {
                                            try {
                                                _job_manager->terminateJob(
                                                    _system_state_tracker->get_running_job(hostname));
                                            }
                                            catch (ExecutionException&) {
                                                std::cerr << "Tried to terminate job on host: " << hostname << " with SRJ VAR" << std::endl;
                                            }
                                        }
                                        _system_state_tracker->untrack_job(hostname);
                                        if (!_system_state_tracker->is_host_down(hostname)) {
                                            NodeKiller::reset_node_killer(
                                                this->getSimulation(),
                                                hostname,
                                                _application_specs->get_exponential_distribution(),
                                                _application_specs->get_restart_overhead(),
                                                this->commport);
                                            _system_state_tracker->reset_host(hostname);
                                        }
                                        else {
                                            _system_state_tracker->reset_host(hostname);
                                            _system_state_tracker->set_host_down(hostname);
                                        }
                                        // _system_state_tracker->untrack_job(hostname);
                                    }
                                }
                            }
                        }



                        // this host has finished their task chain
                        if (_application_specs->get_host_task_to_schedule(success_hostname).empty()) {
                            double final_error_level = success_host_running_error_level;
                            if (repetition_results[repeat].second > final_error_level) {
                                repetition_results[repeat] = {true, final_error_level};
                            }

                            std::cout << "Host " << success_hostname << " succeeded with error: " << final_error_level << std::endl;

                            // once any host finishes, restart every host with the completed chain in consideration
                            if (_temporal_redundancy == "aggressive") {
                                for (const auto& entry : _compute_services) {
                                    std::string hostname = entry.first;

                                    best_error_level_by_host[hostname] = std::min(best_error_level_by_host[hostname], final_error_level);

                                    algorithm->reset_host_preprocessed_decisions(hostname);

                                    _application_specs->update_host_running_data_size(hostname, initial_data_size);
                                    _application_specs->update_host_running_error_level(hostname, initial_error_level);
                                    _application_specs->update_host_task_to_schedule(hostname, 0);
                                    _application_specs->reset_host_current_decision_node(hostname);

                                    _application_specs->reset_host_decision_history(hostname);
                                    _application_specs->clear_decision_tree(hostname);
                                    _application_specs->build_decision_tree(hostname);
                                    _application_specs->prune_decision_tree(hostname, final_error_level);

                                    if (_system_state_tracker->is_a_job_running(hostname)) {
                                        try {
                                            _job_manager->terminateJob(
                                                _system_state_tracker->get_running_job(hostname));
                                        }
                                        catch (ExecutionException&) {
                                            std::cerr << "Tried to terminate job on host: " << hostname << " after finish with TR AGG" << std::endl;
                                        }
                                    }
                                    _system_state_tracker->untrack_job(hostname);
                                    if (!_system_state_tracker->is_host_down(hostname)) {
                                        NodeKiller::reset_node_killer(
                                            this->getSimulation(),
                                            hostname,
                                            _application_specs->get_exponential_distribution(),
                                            _application_specs->get_restart_overhead(),
                                            this->commport);
                                        _system_state_tracker->reset_host(hostname);
                                    }
                                    else {
                                        _system_state_tracker->reset_host(hostname);
                                        _system_state_tracker->set_host_down(hostname);
                                    }
                                    // _system_state_tracker->untrack_job(hostname);
                                }
                            }
                            else if (_temporal_redundancy == "dependent") {
                                for (const auto& entry : _compute_services) {
                                    std::string hostname = entry.first;

                                    if ((!_application_specs->can_possibly_do_better(hostname, success_hostname) || _application_specs->get_host_task_to_schedule(hostname).empty()) && !_system_state_tracker->is_host_finished(hostname)) {
                                        best_error_level_by_host[hostname] = std::min(best_error_level_by_host[hostname], final_error_level);

                                        algorithm->reset_host_preprocessed_decisions(hostname);

                                        _application_specs->update_host_running_data_size(hostname, initial_data_size);
                                        _application_specs->update_host_running_error_level(hostname, initial_error_level);
                                        _application_specs->update_host_task_to_schedule(hostname, 0);
                                        _application_specs->reset_host_current_decision_node(hostname);

                                        _application_specs->reset_host_decision_history(hostname);
                                        _application_specs->clear_decision_tree(hostname);
                                        _application_specs->build_decision_tree(hostname);
                                        _application_specs->prune_decision_tree(hostname, final_error_level);

                                        if (_system_state_tracker->is_a_job_running(hostname)) {
                                            try {
                                                _job_manager->terminateJob(
                                                    _system_state_tracker->get_running_job(hostname));
                                            }
                                            catch (ExecutionException&) {
                                                std::cerr << "Tried to terminate job on host: " << hostname << " after finish with TR DEP" << std::endl;
                                            }
                                        }
                                        _system_state_tracker->untrack_job(hostname);
                                        if (!_system_state_tracker->is_host_down(hostname)) {
                                            NodeKiller::reset_node_killer(
                                                this->getSimulation(),
                                                hostname,
                                                _application_specs->get_exponential_distribution(),
                                                _application_specs->get_restart_overhead(),
                                                this->commport);
                                            _system_state_tracker->reset_host(hostname);
                                        }
                                        else {
                                            _system_state_tracker->reset_host(hostname);
                                            _system_state_tracker->set_host_down(hostname);
                                        }
                                        // _system_state_tracker->untrack_job(hostname);
                                    }
                                }
                            }
                            else if (_temporal_redundancy == "independent") {
                                for (const auto& entry : _compute_services) {
                                    std::string hostname = entry.first;

                                    if (_application_specs->get_host_task_to_schedule(hostname).empty() && !_system_state_tracker->is_host_finished(hostname)) {
                                        best_error_level_by_host[hostname] = _application_specs->get_host_running_error_level(hostname);

                                        algorithm->reset_host_preprocessed_decisions(hostname);

                                        _application_specs->update_host_running_data_size(hostname, initial_data_size);
                                        _application_specs->update_host_running_error_level(hostname, initial_error_level);
                                        _application_specs->update_host_task_to_schedule(hostname, 0);
                                        _application_specs->reset_host_current_decision_node(hostname);

                                        _application_specs->reset_host_decision_history(hostname);
                                        _application_specs->clear_decision_tree(hostname);
                                        _application_specs->build_decision_tree(hostname);
                                        _application_specs->prune_decision_tree(hostname, final_error_level);

                                        if (_system_state_tracker->is_a_job_running(hostname)) {
                                            try {
                                                _job_manager->terminateJob(
                                                    _system_state_tracker->get_running_job(hostname));
                                            }
                                            catch (ExecutionException&) {
                                                std::cerr << "Tried to terminate job on host: " << hostname << " after finish with TR INDEP" << std::endl;
                                            }
                                        }
                                        _system_state_tracker->untrack_job(hostname);
                                        if (!_system_state_tracker->is_host_down(hostname)) {
                                            NodeKiller::reset_node_killer(
                                                this->getSimulation(),
                                                hostname,
                                                _application_specs->get_exponential_distribution(),
                                                _application_specs->get_restart_overhead(),
                                                this->commport);
                                            _system_state_tracker->reset_host(hostname);
                                        }
                                        else {
                                            _system_state_tracker->reset_host(hostname);
                                            _system_state_tracker->set_host_down(hostname);
                                        }
                                        // _system_state_tracker->untrack_job(hostname);
                                    }
                                }
                            }
                            else {
                                for (const auto& entry : _compute_services) {
                                    std::string hostname = entry.first;

                                    if (_application_specs->get_host_task_to_schedule(hostname).empty() && !_system_state_tracker->is_host_finished(hostname)) {
                                        best_error_level_by_host[hostname] = _application_specs->get_host_running_error_level(hostname);

                                        // algorithm->reset_host_preprocessed_decisions(hostname);

                                        _application_specs->update_host_running_data_size(hostname, initial_data_size);
                                        _application_specs->update_host_running_error_level(hostname, initial_error_level);
                                        _application_specs->update_host_task_to_schedule(hostname, 0);
                                        _application_specs->reset_host_current_decision_node(hostname);

                                        _application_specs->reset_host_decision_history(hostname);
                                        // _application_specs->clear_decision_tree(hostname);
                                        // _application_specs->build_decision_tree(hostname);
                                        // _application_specs->prune_decision_tree(hostname, final_error_level);

                                        std::cerr << "Stopping host " << hostname << std::endl;
                                        if (_system_state_tracker->is_a_job_running(hostname)) {
                                            try {
                                                _job_manager->terminateJob(
                                                    _system_state_tracker->get_running_job(hostname));
                                            }
                                            catch (ExecutionException&) {
                                                std::cerr << "Tried to terminate job on host: " << hostname << " after finish with TR OFF" << std::endl;
                                            }
                                        }
                                        _system_state_tracker->untrack_job(hostname);
                                        NodeKiller::stop_node_killer(hostname);
                                        _system_state_tracker->reset_host(hostname);
                                        _system_state_tracker->set_host_down(hostname);
                                        // _system_state_tracker->untrack_job(hostname);
                                        _system_state_tracker->set_host_finished(hostname);
                                    }
                                }
                            }

                            if (_application_specs->decision_tree_empty(success_hostname)) {
                                std::cout << "Best possible error level achieved." << std::endl;
                                // algorithm->reset_all_preprocessed_decisions();
                                _system_state_tracker->reset_all_hosts();
                                break;
                            }

                            if (_system_state_tracker->are_all_hosts_finished()) {
                                std::cout << "All hosts finished." << std::endl;
                                // algorithm->reset_all_preprocessed_decisions();
                                _system_state_tracker->reset_all_hosts();
                                break;
                            }
                        }
                        else {
                            // reset the success node if its not the last task
                            if (_system_state_tracker->is_a_job_running(success_hostname)) {
                                try {
                                    _job_manager->terminateJob(
                                        _system_state_tracker->get_running_job(success_hostname));
                                }
                                catch (ExecutionException&) {
                                    std::cerr << "Tried to terminate job on host: " << success_hostname << " after task success" << std::endl;
                                }
                            }
                            _system_state_tracker->untrack_job(success_hostname);
                            NodeKiller::reset_node_killer(this->getSimulation(),
                                               success_hostname,
                                               _application_specs->get_exponential_distribution(),
                                               _application_specs->get_restart_overhead(),
                                               this->commport);
                            _system_state_tracker->reset_host(success_hostname);
                            // _system_state_tracker->untrack_job(success_hostname);
                        }
                    }
                    else if (auto timer_event = std::dynamic_pointer_cast<TimerEvent>(event)) {
                        // This is the catch-all timer-based stuff
                        std::string timeout_prefix = "time_out";
                        std::string hostup_prefix = "host_up";
                        std::string hostdown_prefix = "host_down";

                        // Is it a timeout?
                        if (timer_event->message.compare(0, timeout_prefix.length(), timeout_prefix) == 0) {
                            // Check if the colon and hyphen exist
                            size_t colon_pos = timer_event->message.find(':');
                            size_t hyphen_pos = timer_event->message.find('-');

                            std::string algorithm_name = timer_event->message.substr(colon_pos + 1, hyphen_pos - colon_pos - 1);
                            std::string repeat_id = timer_event->message.substr(hyphen_pos + 1);
                            if (algorithm_name != algorithm->get_name() || repeat_id != std::to_string(repeat)) {
                                continue; // Spurious timeout
                            }
                            if (!repetition_results[repeat].first) {
                                std::cout << "REPETITION " << std::to_string(repeat) << " HAS FAILED (after " <<
                                Simulation::getCurrentSimulatedDate() - repeat_start_date << " seconds)" << std::endl;
                            }
                            WRENCH_INFO("Deadline reached");
                            std::cout << "Error: " << repetition_results[repeat].second << std::endl;

                            // algorithm->reset_all_preprocessed_decisions();
                            this->restart_system();
                            break;
                        }

                        if (timer_event->message.compare(0, hostup_prefix.length(), hostup_prefix) == 0) {
                            size_t pos = timer_event->message.find(':');
                            std::string hostname = timer_event->message.substr(pos + 1);
                            // Reset the host's entry to nullptr, so that we now know it's idle
                            std::cerr << "Host " << hostname.c_str() << " is back up at time " << Simulation::getCurrentSimulatedDate() - repeat_start_date << std::endl;

                            WRENCH_INFO("Was notified that %s is up again", hostname.c_str());
                            _system_state_tracker->set_host_up(hostname);
                            continue;
                        }

                        if (timer_event->message.compare(0, hostdown_prefix.length(), hostdown_prefix) == 0) {
                            size_t pos = timer_event->message.find(':');
                            std::string hostname = timer_event->message.substr(pos + 1);
                            std::cerr << "Host " << hostname.c_str() << " is down at time " << Simulation::getCurrentSimulatedDate() - repeat_start_date << std::endl;

                            // Cancel the job
                            WRENCH_INFO("Was notified that %s is down... terminating job of need be", hostname.c_str());
                            if (_system_state_tracker->is_a_job_running(hostname)) {
                                try {
                                    _job_manager->terminateJob(
                                        _system_state_tracker->get_running_job(hostname));
                                }
                                catch (ExecutionException&) {
                                    std::cerr << "Tried to terminate job on down host: " << hostname << " for host going down" << std::endl;
                                }
                            }

                            // Could any of these functions be combined?
                            _system_state_tracker->untrack_job(hostname);
                            _system_state_tracker->reset_host(hostname);
                            _system_state_tracker->set_host_down(hostname);
                        }
                    }
                }

                // Cancel all pending jobs as we're done
                for (const auto& entry : _compute_services) {
                    std::string hostname = entry.first;
                    if (_system_state_tracker->is_a_job_running(hostname)) {
                        try {
                            _job_manager->terminateJob(
                                _system_state_tracker->get_running_job(hostname));
                        }
                        catch (ExecutionException&) {
                            std::cerr << "Tried to terminate job on host: " << hostname << " at the end of the repetition" << std::endl;
                        }
                    }
                }
            }

            int num_successes = 0;
            double cumulative_error_level_successes = 0.0;
            double cumulative_error_level = 0.0;
            for (int repeat = 0; repeat < _num_repeats; repeat++) {
                if (repetition_results[repeat].first) {
                    num_successes++;
                    cumulative_error_level_successes += repetition_results[repeat].second;
                }
                cumulative_error_level += repetition_results[repeat].second;
            }

            std::cout << "Total repeats: " << _num_repeats << "\n";
            std::cout << "Num successes: " << num_successes << "\n";
            std::cout << "Success rate: " << static_cast<double>(num_successes)/static_cast<double>(_num_repeats) << "\n";
            std::cout << "Avg error level: " << cumulative_error_level/static_cast<double>(_num_repeats) << "\n";
            if (num_successes > 0) {
                std::cout << "Avg error level of successes: " << cumulative_error_level_successes/static_cast<double>(num_successes) << "\n\n";
            }
            else {
                std::cout << "Avg error level of successes: N/A\n\n";
            }
            std::cout << "FINAL RESULTS PER REPETITION:\n";
            for (int repeat = 0; repeat < _num_repeats; repeat++) {
                std::cout << "Repetition " << repeat << ": " << repetition_results[repeat].second << "\n";
            }
        }
        return 0;
    }
} // namespace wrench
