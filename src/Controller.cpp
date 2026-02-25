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
                                                          _compute_services(compute_services) {

        _num_repeats = boost::json::value_to<long>(_execution_spec.at("num_repeats"));
        _temporal_redundancy = boost::json::value_to<bool>(_scheduling_spec.at("hacks").as_object().at("temporal_redundancy"));
        _stop_running_jobs = boost::json::value_to<bool>(_scheduling_spec.at("hacks").as_object().at("stop_running_jobs"));

        /* Create the data structure that describes all task functions */
        for (const auto& task : _application_spec.at("tasks").as_array()) {
            std::vector<std::pair<std::string, std::function<double(double, double)>>> options_for_task;

            auto task_name = boost::json::value_to<std::string>(task.as_object().at("name"));
            auto& exec_options = task.as_object().at("execution_options").as_array();

            for (const auto& option : exec_options) {
                auto option_name = boost::json::value_to<std::string>(option.as_object().at("name"));

                for (auto function_name : {"t_function", "d_function", "e_function"}) {
                    auto& function = option.as_object().at(function_name).as_object();
                    auto func = FunctionGenerator::get_function(function);

                    if (static_cast<std::string>(function_name) == "e_function") {
                        options_for_task.emplace_back(option_name, func);
                    }
                    _task_functions[task_name][option_name][function_name] = func;
                }
            }
        }

        /* Create the probability computation utility */
        _probability_computation = std::make_unique<ProbabilityComputation>(_application_specs);

        /* Determine the list of scheduling algorithms */
        /* Determine/validate the scheduling algorithms to use */
        std::string scheduling_type;
        if (_application_spec.at("tasks").as_array().size() == 1) {
            scheduling_type = "one_task";
        }
        else {
            scheduling_type = "multi_task";
        }
        for (auto const& alg_name : _scheduling_spec.at("algorithms").at(scheduling_type).as_array()) {
            auto alg = SchedulingAlgorithm::create_scheduling_algorithm(
                            boost::json::value_to<string>(alg_name), _application_specs, _task_functions, _probability_computation.get());

            _scheduling_algorithms.push_back(alg);
        }

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

        const auto compute_action = job->addComputeAction("compute",
                                                    _task_functions[task_name][execution_option]["t_function"]
                                                    (running_output_data_size, running_output_error_level),
                                                    0.0,
                                                    1, 1, ParallelModel::CONSTANTEFFICIENCY(1.0));

        const auto write_output_action = job->addCustomAction("write", 0, 1,
                                                        [this, task_name, execution_option, running_output_data_size,
                                                            running_output_error_level](
                                                        const std::shared_ptr<wrench::ActionExecutor>&
                                                        action_executor) {
                                                            auto disk = wrench::S4U_Simulation::hostHasMountPoint(action_executor->hostname, "/");
                                                            disk->write(static_cast<sg_size_t>(
                                                                _task_functions[task_name][execution_option][
                                                                    "d_function"]
                                                                (running_output_data_size,
                                                                 running_output_error_level)));
                                                        },
                                                        [](const std::shared_ptr<wrench::ActionExecutor>&
                                                        action_executor) {
                                                        });

        job->addActionDependency(read_input_action, compute_action);
        job->addActionDependency(compute_action, write_output_action);

        WRENCH_INFO("Submitting a new job to %s", hostname.c_str());
        _job_manager->submitJob(job, _compute_services.at(hostname));

        _system_state_tracker->track_job(job, hostname, task_name, execution_option, Simulation::getCurrentSimulatedDate());
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
            // std::cerr << "** " << algorithm->get_name().c_str() << " **" << std::endl;
            WRENCH_INFO("** Running experiments with algorithm '%s' **", algorithm->get_name().c_str());

            /* Keep track of number of successes */
            int num_successes = 0;
            double cumulative_error_level = 0.0;

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
                this->setTimer(time_to_deadline, "time_out:" + std::to_string(repeat));

                /* Running values of output data size and error level */
                auto running_output_data_size = initial_data_size;
                auto running_output_error_level = initial_error_level;
                double best_error = _application_specs->get_e_fail();

                /* Current task is the first task */
                int current_task_counter = 0;
                auto current_task = _application_specs->get_task(0);

                /* Build/reset decision tree to use for temporal redundancy */
                _application_specs->prune_decision_tree(0.0);
                _application_specs->build_decision_tree(_task_functions);
                algorithm->preprocess_decisions(initial_data_size, initial_error_level,
                                _application_specs->get_deadline(), OPTIMISTIC_DISCRETIZATION);

                /* Reset all current decision nodes for all hosts in the state tracker */
                _system_state_tracker->reset_all_hosts();
                _system_state_tracker->initialize_all_hosts_decision_nodes(_application_specs->get_decision_tree_root());

                /* Loop until an event message arrives */
                while (true) {
                    // Invoke the scheduler
                    auto decisions =
                        algorithm->make_decisions(_system_state_tracker.get(),
                           current_task,
                           time_to_deadline - Simulation::getCurrentSimulatedDate(),
                           true);

                    // Implement the scheduling decisions
                    for (const auto &[hostname, task, execution_option] : decisions) {
                        // std::cerr << "Scheduling decision: run task " << task <<
                        //     " with option " << execution_option <<
                        //     " on host " << hostname << " at time " << Simulation::getCurrentSimulatedDate()  - repeat_start_date <<std::endl;
                        this->submit_job(task,
                                         execution_option,
                                         running_output_data_size,
                                         running_output_error_level,
                                         hostname);
                    }

                    // Here we could instead call waitForAndProcessNextEvent() and define the handling
                    // methods, in case this if-else-if thing becomes too unwieldly
                    auto event = this->waitForNextEvent();
                    if (auto success_event = std::dynamic_pointer_cast<CompoundJobCompletedEvent>(event)) {
                        auto success_hostname = success_event->compute_service->getHosts().at(0);

                        auto job_task_name = success_event->job->getName();
                        if (job_task_name.compare(0, current_task.length(), current_task)) {
                            // std::cout << "Got a job success message for the wrong task." << std::endl;
                            _system_state_tracker->reset_host(success_hostname);
                            _system_state_tracker->untrack_job(success_hostname);
                            continue;
                        }
                        // std::cout << job_task_name << " completed successfully by host " << success_hostname << " at " << (Simulation::getCurrentSimulatedDate() - repeat_start_date) << std::endl;

                        std::string selected_option = job_task_name.substr(current_task.length() + 1);
                        std::string completed_task = current_task;
                        running_output_data_size = _task_functions.at(current_task).at(selected_option).at("d_function")(running_output_data_size, running_output_error_level);;
                        running_output_error_level = _task_functions.at(current_task).at(selected_option).at("e_function")(running_output_data_size, running_output_error_level);
                        current_task_counter++;
                        current_task = _application_specs->get_task(current_task_counter);

                        if (current_task.empty()) {
                            // were done
                            if (best_error == _application_specs->get_e_fail()) {
                                num_successes++;
                            }
                            best_error = running_output_error_level;

                            std::cout << "Succeeded with error: " << running_output_error_level << std::endl;

                            if (!_temporal_redundancy) {
                                // std::cout << "Best error: " << best_error << std::endl;
                                cumulative_error_level += best_error;
                                algorithm->reset_preprocessed_decisions();
                                _system_state_tracker->reset_all_hosts();
                                this->restart_system();
                                break;
                            }

                            // reset running trackers
                            running_output_data_size = initial_data_size;
                            running_output_error_level = initial_error_level;
                            current_task_counter = 0;
                            current_task = _application_specs->get_task(current_task_counter);

                            _application_specs->prune_decision_tree(best_error);
                            if (_application_specs->decision_tree_empty()) {
                                // std::cout << "We cannot do better" << std::endl;
                                cumulative_error_level += best_error;
                                break;
                            }
                            algorithm->reset_preprocessed_decisions();
                            _system_state_tracker->reset_all_hosts();
                            _system_state_tracker->initialize_all_hosts_decision_nodes(_application_specs->get_decision_tree_root());

                            algorithm->preprocess_decisions(initial_data_size, initial_error_level,
                                _application_specs->get_deadline(),
                                OPTIMISTIC_DISCRETIZATION);
                        } else {
                            // were not done, update all hosts decision nodes to reflect completed task
                            _system_state_tracker->update_all_hosts_decision_nodes(success_hostname, completed_task, selected_option);
                        }

                        // std::cout << "New current task: " << current_task << std::endl;

                        if (_stop_running_jobs) {
                            this->restart_system();
                            for (int i = 0; i < _application_specs->get_num_compute_nodes(); i++) {
                                std::string hostname = "ComputeHost_" + std::to_string(i);
                                // std::cout << "Resetting host " << hostname << std::endl;
                                if (!_system_state_tracker->is_host_down(hostname)) {
                                    NodeKiller::reset_node_killer(this->getSimulation(),
                                        hostname,
                                        _application_specs->get_exponential_distribution(),
                                        _application_specs->get_restart_overhead(),
                                        this->commport);
                                }
                            }
                        } else {
                            _system_state_tracker->reset_host(success_hostname);
                            _system_state_tracker->untrack_job(success_hostname);
                            NodeKiller::reset_node_killer(this->getSimulation(),
                                               success_hostname,
                                               _application_specs->get_exponential_distribution(),
                                               _application_specs->get_restart_overhead(),
                                               this->commport);
                        }
                    }
                    else if (auto timer_event = std::dynamic_pointer_cast<TimerEvent>(event)) {
                        // This is the catch-all timer-based stuff
                        std::string timeout_prefix = "time_out";
                        std::string hostup_prefix = "host_up";
                        std::string hostdown_prefix = "host_down";

                        // Is it a timeout?
                        if (timer_event->message.compare(0, timeout_prefix.length(), timeout_prefix) == 0) {
                            size_t pos = timer_event->message.find(':');
                            // Check if the colon exists
                            std::string repeat_id = timer_event->message.substr(pos + 1);
                            if (repeat_id != std::to_string(repeat)) {
                                continue; // Spurious timeout
                            }
                            if (best_error == _application_specs->get_e_fail()) {
                                // std::cout << "REPETITION " << std::to_string(repeat) << " HAS FAILED (after " <<
                                // Simulation::getCurrentSimulatedDate() - repeat_start_date << " seconds)" << std::endl;
                            }
                            WRENCH_INFO("Deadline reached");
                            // std::cout << "Best error: " << best_error << std::endl;
                            cumulative_error_level += best_error;

                            algorithm->reset_preprocessed_decisions();
                            this->restart_system();
                            break;
                        }

                        if (timer_event->message.compare(0, hostup_prefix.length(), hostup_prefix) == 0) {
                            size_t pos = timer_event->message.find(':');
                            std::string hostname = timer_event->message.substr(pos + 1);
                            // Reset the host's entry to nullptr, so that we now know it's idle
                            // std::cerr << "Host " << hostname.c_str() << " is back up at time " << Simulation::getCurrentSimulatedDate() - repeat_start_date << std::endl;

                            WRENCH_INFO("Was notified that %s is up again", hostname.c_str());
                            _system_state_tracker->set_host_up(hostname);
                            continue;
                        }

                        if (timer_event->message.compare(0, hostdown_prefix.length(), hostdown_prefix) == 0) {
                            size_t pos = timer_event->message.find(':');
                            std::string hostname = timer_event->message.substr(pos + 1);
                            // std::cerr << "Host " << hostname.c_str() << " is down at time " << Simulation::getCurrentSimulatedDate() - repeat_start_date << std::endl;

                            // Cancel the job
                            WRENCH_INFO("Was notified that %s is down... terminating job of need be", hostname.c_str());
                            if (_system_state_tracker->is_a_job_running(hostname)) {
                                try {
                                    _job_manager->terminateJob(
                                        _system_state_tracker->get_running_job(hostname));
                                }
                                catch (ExecutionException&) {
                                    // std::cerr << "Tried to terminate job on down host: " << hostname << " for host going down" << std::endl;
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
                            // std::cerr << "Tried to terminate job on down host: " << hostname << " at the end of the repetition" << std::endl;
                        }
                    }
                }
            }

            std::cout << "Total repeats: " << _num_repeats << "\n";
            std::cout << "Num successes: " << num_successes << "\n";
            std::cout << "Success rate: " << static_cast<double>(num_successes)/static_cast<double>(_num_repeats) << "\n";
            std::cout << "Avg error level: " << cumulative_error_level/static_cast<double>(_num_repeats) << "\n";
        }
        return 0;
    }
} // namespace wrench
