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

#include <iostream>
#include <wrench/util/UnitParser.h>

#include "Controller.h"
#include "FunctionGenerator.h"
#include "NodeKiller.h"
#include "ProbabilityComputation.h"
#include "RunningJob.h"
#include "JobTracker.h"
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
     * @param storage_service: the storage service
     * @param hostname: the name of the host on which to start the Execution Controller
     */
    Controller::Controller(const boost::json::object& application_spec,
                           const boost::json::object& execution_spec,
                           const boost::json::object& scheduling_spec,
                           const std::shared_ptr<ApplicationSpecs>& application_specs,
                           const std::map<std::string, std::shared_ptr<BareMetalComputeService>>& compute_services,
                           const std::shared_ptr<SimpleStorageService>& storage_service,
                           const std::string& hostname) : ExecutionController(hostname, "controller"),
                                                          _application_specs(application_specs),
                                                          _application_spec(application_spec),
                                                          _execution_spec(execution_spec),
                                                          _scheduling_spec(scheduling_spec),
                                                          _compute_services(compute_services),
                                                          _storage_service(storage_service) {

        _num_repeats = boost::json::value_to<long>(_execution_spec.at("num_repeats"));

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

        /* Build decision tree to use for temporal redundancy */
        _application_specs->build_decision_tree(_task_functions);

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
                            boost::json::value_to<string>(alg_name), _application_specs);

            _scheduling_algorithms.push_back(alg);
        }

        _storage_disk = _storage_service->getHost()->get_disks().at(0);
    }

    /**
     * @brief Helper function to create and submit a job
     * @param job_tracker The running job tracker
     * @param task_name The task name
     * @param execution_option The execution option
     * @param running_output_data_size The running data size
     * @param running_output_error_level The running error
     * @param hostname The hostname on which to start the job
     * @return a job
     */
    void Controller::submit_job(const std::shared_ptr<JobTracker>& job_tracker,
                                const std::string& task_name,
                                const std::string& execution_option,
                                double running_output_data_size,
                                double running_output_error_level,
                                const std::string& hostname) {
        const auto job = _job_manager->createCompoundJob(task_name + "_" + execution_option);

        const auto read_input_action = job->addCustomAction("read", 0, 1,
                                                      [this, running_output_data_size](
                                                      const std::shared_ptr<wrench::ActionExecutor>& action_executor) {
                                                          _storage_disk->write(
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
                                                            _storage_disk->read(static_cast<sg_size_t>(
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
        job_tracker->track_job(job, hostname, task_name, execution_option);
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

        /* Create a job manager so that we can create/submit jobs */
        _job_manager = this->createJobManager();

        /* Create the probability computation utility */
        _probability_computation = std::make_unique<ProbabilityComputation>(_application_specs);

        /* Create an execution option comparator function object */
        _option_comparator = std::make_shared<ExpectedErrorComparator>(_application_specs);

        /* Get initial x (data size) and y (error) from the JSON file */
        auto initial_data_size = boost::json::value_to<double>(_application_spec.at("initial_data_size"));
        auto initial_error_level = boost::json::value_to<double>(_application_spec.at("initial_error_level"));

        /* Loop over all the scheduling algorithms */
        for (const auto& algorithm : _scheduling_algorithms) {
            std::cerr << "** " << algorithm->get_name().c_str() << " **" << std::endl;
            WRENCH_INFO("** Running experiments with algorithm '%s' **", algorithm->get_name().c_str());

            /* Keep track of number of successes */
            int num_successes = 0;

            /* Do all the repeats */
            for (int repeat = 0; repeat < _num_repeats; repeat++) {
                double repeat_start_date = Simulation::getCurrentSimulatedDate();

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

                /* Create the job tracker */
                std::vector<std::string> hostnames;
                for (auto const &entry : _compute_services) { hostnames.push_back(entry.first); }
                auto job_tracker = JobTracker::create_tracker(hostnames);

                /* Running values of output data size and error level */
                auto running_output_data_size = initial_data_size;
                auto running_output_error_level = initial_error_level;
                double best_error = _application_specs->get_e_fail();

                /* Current task is the first task */
                int current_task_counter = 0;
                auto current_task = _application_specs->get_task(current_task_counter);

                /* Loop until the task completes successfully somewhere */
                /* (right now this assumes a single-task applications)  */
                while (true) {

                    // Invoke the scheduler
                    auto decisions =
                        algorithm->make_decisions(job_tracker.get(),
                        _probability_computation.get(),
                           _task_functions,
                           current_task,
                           running_output_data_size, running_output_error_level,
                           time_to_deadline - Simulation::getCurrentSimulatedDate(),
                           _option_comparator.get(), true);

                    // Implement the scheduling decisions
                    for (auto const &decision : decisions) {
                        // std::cerr << "Scheduling decision: run task " << decision.task <<
                        //     " with option " << decision.execution_option <<
                        //     " on host " << decision.hostname << " at time " << Simulation::getCurrentSimulatedDate()  - repeat_start_date <<std::endl;
                        _application_specs->update_running_host(decision.hostname, decision.task,
                                                                decision.execution_option,
                                                                Simulation::getCurrentSimulatedDate());
                        this->submit_job(job_tracker,
                                         decision.task,
                                         decision.execution_option,
                                         running_output_data_size,
                                         running_output_error_level,
                                         decision.hostname);
                    }

                    // Here we could instead call waitForAndProcessNextEvent() and define the handling
                    // methods, in case this if-else-if thing becomes too unwieldly
                    auto event = this->waitForNextEvent();
                    if (auto success_event = std::dynamic_pointer_cast<CompoundJobCompletedEvent>(event)) {
                        auto hostname = success_event->compute_service->getHosts().at(0);
                        auto job_task_name = success_event->job->getName();
                        if (job_task_name.compare(0, current_task.length(), current_task)) {
                            std::cout << "Got a job success message for the wrong task." << std::endl;
                            continue;
                        }
                        std::cout << job_task_name << " completed successfully." << std::endl;

                        std::string selected_option = job_task_name.substr(current_task.length() + 1);
                        running_output_data_size = _task_functions.at(current_task).at(selected_option).at("d_function")(running_output_data_size, running_output_error_level);;
                        running_output_error_level = _task_functions.at(current_task).at(selected_option).at("e_function")(running_output_data_size, running_output_error_level);
                        current_task_counter++;
                        current_task = _application_specs->get_task(current_task_counter);

                        if (current_task.empty()) {
                            // were done

                            if (best_error == _application_specs->get_e_fail()) {
                                num_successes++;
                                std::cout << "REPETITION " << std::to_string(repeat) << " HAS SUCCEEDED (after " <<
                                    Simulation::getCurrentSimulatedDate()  - repeat_start_date << " seconds)" << std::endl;
                            } else {
                                std::cout << "REPETITION " << std::to_string(repeat) << " HAS IMPROVED ITS ERROR LEVEL (after " <<
                                    Simulation::getCurrentSimulatedDate()  - repeat_start_date << " seconds)" << std::endl;
                            }
                            std::cout << "Previous error lvl: " << best_error << std::endl;
                            std::cout << "New best error lvl: " << running_output_error_level << std::endl;

                            // reset running trackers
                            best_error = running_output_error_level;
                            running_output_data_size = initial_data_size;
                            running_output_error_level = initial_error_level;
                            current_task_counter = 0;
                            current_task = _application_specs->get_task(current_task_counter);

                            _application_specs->prune_decision_tree(best_error);
                            if (_application_specs->decision_tree_empty()) {
                                std::cout << "We cannot do better" << std::endl;
                                break;
                            }
                            algorithm->reset_preprocessed_decisions();
                        }

                        for (int i = 0; i < _application_specs->get_num_compute_nodes(); i++) {
                            std::string hostname_loop = "ComputeHost_" + std::to_string(i);
                            std::cout << "Resetting host " << hostname_loop << std::endl;
                            if (job_tracker->is_a_job_running(hostname_loop) && hostname_loop != hostname) {
                                std::cout << "Job is running on " << hostname_loop << ", terminating it" << std::endl;
                                try {
                                    _job_manager->terminateJob(
                                        job_tracker->get_running_job(hostname)->get_compound_job());
                                }
                                catch (ExecutionException&) {
                                    std::cerr << "Tried to terminate job on down host: " << hostname_loop << std::endl;
                                }
                                job_tracker->untrack_job(hostname_loop);
                            }
                            _application_specs->reset_running_host(hostname_loop);
                        }
                        job_tracker->untrack_job(hostname);

                        NodeKiller::start_node_killers(this->getSimulation(),
                                               _compute_services,
                                               _application_specs->get_seed(),
                                               false,
                                               _application_specs->get_exponential_distribution(),
                                               _application_specs->get_restart_overhead(),
                                               this->commport);

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
                                std::cout << "REPETITION " << std::to_string(repeat) << " HAS FAILED (after " <<
                                Simulation::getCurrentSimulatedDate() - repeat_start_date << " seconds)" << std::endl;
                            }
                            WRENCH_INFO("Deadline reached");
                            _application_specs->reset_all_running_hosts();
                            break;
                        }

                        if (timer_event->message.compare(0, hostup_prefix.length(), hostup_prefix) == 0) {
                            size_t pos = timer_event->message.find(':');
                            std::string hostname = timer_event->message.substr(pos + 1);
                            // Reset the host's entry to nullptr, so that we now know it's idle
                            std::cerr << "Host " << hostname.c_str() << " is back up at time " << Simulation::getCurrentSimulatedDate() - repeat_start_date << std::endl;
                            WRENCH_INFO("Was notified that %s is up again", hostname.c_str());
                            // NOTE: Why does it not untrack the job immediately after terminating?
                            job_tracker->untrack_job(hostname);
                            continue;
                        }

                        if (timer_event->message.compare(0, hostdown_prefix.length(), hostdown_prefix) == 0) {
                            size_t pos = timer_event->message.find(':');
                            std::string hostname = timer_event->message.substr(pos + 1);
                            std::cerr << "Host " << hostname.c_str() << " is down at time " << Simulation::getCurrentSimulatedDate() - repeat_start_date << std::endl;
                            _application_specs->reset_running_host(hostname);

                            // Cancel the job
                            WRENCH_INFO("Was notified that %s is down... terminating job of need be", hostname.c_str());
                            if (job_tracker->is_a_job_running(hostname)) {
                                _job_manager->terminateJob(
                                    job_tracker->get_running_job(hostname)->get_compound_job());
                            }
                        }
                    }
                }

                // Cancel all pending jobs as we're done
                for (const auto& entry : _compute_services) {
                    std::string hostname = entry.first;
                    if (job_tracker->is_a_job_running(hostname)) {
                        try {
                            _job_manager->terminateJob(
                                job_tracker->get_running_job(hostname)->get_compound_job());
                        }
                        catch (ExecutionException&) {
                        }
                    }
                }
            }
        }
        return 0;
    }
} // namespace wrench
