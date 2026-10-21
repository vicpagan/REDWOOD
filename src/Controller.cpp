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
#include <limits>

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
     * @param application_spec: application specification
     * @param execution_spec: execution specification
     * @param scheduling_spec: scheduling specification
     * @param application_spec: application specifications
     * @param compute_services: a set of compute services available to run actions
     * @param fake_io: Whether to fake I/O operations or not
     * @param hostname: the name of the host on which to start the Execution Controller
     */
    Controller::Controller(const boost::json::object &application_spec,
                           const boost::json::object &execution_spec,
                           const boost::json::object &scheduling_spec,
                           const std::shared_ptr<ApplicationSpecs> &application_specs,
                           const std::map<std::string, std::shared_ptr<BareMetalComputeService> > &compute_services,
                           const bool fake_io,
                           const std::string &hostname) : ExecutionController(hostname, "controller"),
                                                          _application_specs(application_specs),
                                                          _application_spec(application_spec),
                                                          _execution_spec(execution_spec),
                                                          _scheduling_spec(scheduling_spec),
                                                          _compute_services(compute_services),
                                                          _fake_io(fake_io),
                                                          _task_functions(_application_specs->get_task_functions()) {
        _num_repeats = boost::json::value_to<long>(_execution_spec.at("num_repeats"));
        _temporal_redundancy = boost::json::value_to<std::string>(
            _scheduling_spec.at("hacks").as_object().at("temporal_redundancy"));
        _stop_running_jobs = boost::json::value_to<std::string>(
            _scheduling_spec.at("hacks").as_object().at("stop_running_jobs"));
        _keep_preprocessed_decisions = (_temporal_redundancy == "off" && _stop_running_jobs == "off");

        /* Create the probability computation utility */
        _probability_computation = std::make_unique<ProbabilityComputation>(_application_specs);

        /* Determine the list of scheduling algorithms */
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
    void Controller::submit_job(const std::string &task_name,
                                const std::string &execution_option,
                                double running_output_data_size,
                                double running_output_error_level,
                                const std::string &hostname) {
        const auto job = _job_manager->createCompoundJob(task_name + "_" + execution_option);

        WRENCH_INFO("Submitting job for task %s with execution option %s on host %s", task_name.c_str(),
                    execution_option.c_str(), hostname.c_str());

        if (_fake_io) {
            job->addCustomAction(
                "fake_io_task", 0, 1,
                [this, task_name, execution_option,
                    running_output_error_level, running_output_data_size
                ](
            const std::shared_ptr<wrench::ActionExecutor> &
            action_executor) {
                    auto disk =
                            wrench::S4U_Simulation::hostHasMountPoint(
                                action_executor->getHostname(), "/");
                    double read_time = running_output_data_size /
                                       disk->get_read_bandwidth();
                    double flops = _task_functions.at(task_name).at(
                        execution_option).at(
                        "t_function")
                    (running_output_data_size,
                     running_output_error_level);
                    double compute_time = flops /
                                          S4U_Simulation::getHostFlopRate(
                                              action_executor->getHostname());

                    auto num_out_bytes = _task_functions.at(task_name).
                            at(execution_option)
                            .at("d_function")
                            (running_output_data_size,
                             running_output_error_level);

                    double write_time = num_out_bytes / disk->
                                        get_write_bandwidth();

                    S4U_Simulation::sleep(read_time + compute_time + write_time);
                },
                [](const std::shared_ptr<wrench::ActionExecutor> &
            action_executor) {
                });
        } else {
            const auto read_input_action =
                    job->addCustomAction(
                        "read", 0, 1,
                        [this, running_output_data_size](
                    const std::shared_ptr<wrench::ActionExecutor> &
                    action_executor) {
                            auto disk =
                                    wrench::S4U_Simulation::hostHasMountPoint(
                                        action_executor->getHostname(), "/");
                            if (_fake_io) {
                                Simulation::sleep(running_output_data_size /
                                                  disk->get_read_bandwidth());
                            } else {
                                disk->write(
                                    static_cast<sg_size_t>(
                                        running_output_data_size));
                            }
                        },
                        [](const std::shared_ptr<wrench::ActionExecutor> &
                    action_executor) {
                        });

            WRENCH_INFO("Read input action for task %s with execution option %s on host %s", task_name.c_str(),
                        execution_option.c_str(), hostname.c_str());

            const auto compute_action =
                    job->addComputeAction(
                        "compute",
                        _task_functions.at(task_name).at(execution_option).at(
                            "t_function")
                        (running_output_data_size, running_output_error_level),
                        0.0,
                        1, 1, ParallelModel::CONSTANTEFFICIENCY(1.0));

            WRENCH_INFO("Compute action for task %s with execution option %s on host %s", task_name.c_str(),
                        execution_option.c_str(), hostname.c_str());

            const auto write_output_action =
                    job->addCustomAction(
                        "write", 0, 1,
                        [this, task_name, execution_option,
                            running_output_data_size,
                            running_output_error_level](
                    const std::shared_ptr<wrench::ActionExecutor> &
                    action_executor) {
                            auto num_bytes = _task_functions.at(task_name).at(
                                        execution_option)
                                    .at("d_function")
                                    (running_output_data_size,
                                     running_output_error_level);
                            auto disk =
                                    wrench::S4U_Simulation::hostHasMountPoint(
                                        action_executor->getHostname(), "/");
                            if (_fake_io) {
                                Simulation::sleep(
                                    num_bytes / disk->get_write_bandwidth());
                            } else {
                                disk->write(
                                    static_cast<sg_size_t>(num_bytes));
                            }
                        },
                        [](const std::shared_ptr<wrench::ActionExecutor> &
                    action_executor) {
                        });

            WRENCH_INFO("Write output action for task %s with execution option %s on host %s", task_name.c_str(),
                        execution_option.c_str(), hostname.c_str());

            job->addActionDependency(read_input_action, compute_action);
            job->addActionDependency(compute_action, write_output_action);
        }

        WRENCH_INFO("Submitting a new job to %s", hostname.c_str());
        _job_manager->submitJob(job, _compute_services.at(hostname));

        _system_state_tracker->track_job(job, hostname, task_name, execution_option,
                                         Simulation::getCurrentSimulatedDate());
    }

    /**
     * @brief Terminate the job running on a host, if any
     * @param hostname The host
     * @param context Description of the situation, for the error message
     */
    void Controller::terminate_running_job(const std::string &hostname, const std::string &context) const {
        if (_system_state_tracker->is_a_job_running(hostname)) {
            try {
                _job_manager->terminateJob(_system_state_tracker->get_running_job(hostname));
            } catch (ExecutionException &) {
                std::cerr << "Tried to terminate job on host: " << hostname << " " << context << std::endl;
            }
        }
    }

    /**
     * @brief Make a host restart the application from the first task, with a decision tree pruned so
     *        that only option sequences with an error lower than prune_threshold are considered
     *        (temporal redundancy)
     * @param hostname The host
     * @param algorithm The scheduling algorithm
     * @param prune_threshold The error level to improve on
     * @param tree_prune_threshold_by_host The current prune thresholds (updated)
     * @param terminate_job Whether to terminate the job currently running on the host, if any
     * @param context Description of the situation, for error messages
     */
    void Controller::restart_host_from_first_task(const std::string &hostname,
                                                  const std::shared_ptr<SchedulingAlgorithm> &algorithm,
                                                  const double prune_threshold,
                                                  std::map<std::string, double> &tree_prune_threshold_by_host,
                                                  const bool terminate_job,
                                                  const std::string &context) {
        algorithm->reset_host_preprocessed_decisions(hostname);

        _application_specs->update_host_running_data_size(hostname, _application_specs->get_initial_data_size());
        _application_specs->update_host_running_error_level(hostname, _application_specs->get_initial_error_level());
        _application_specs->update_host_task_to_schedule(hostname, 0);
        _application_specs->reset_host_current_decision_node(hostname);
        _application_specs->reset_host_decision_history(hostname);

        _application_specs->clear_decision_tree(hostname);
        _application_specs->build_decision_tree(hostname);
        _application_specs->prune_decision_tree(hostname, prune_threshold);
        tree_prune_threshold_by_host[hostname] = prune_threshold;

        if (terminate_job) {
            terminate_running_job(hostname, context);
        }
        _system_state_tracker->untrack_job(hostname);
        const bool is_down = _system_state_tracker->is_host_down(hostname);
        _system_state_tracker->reset_host(hostname);
        if (is_down) {
            _system_state_tracker->set_host_down(hostname);
        }
    }

    /**
     * @brief Make a host abandon its execution and continue from the output just produced by
     *        success_hostname (reactive rescheduling)
     * @param hostname The host
     * @param success_hostname The host that just completed a task
     * @param algorithm The scheduling algorithm
     * @param prune_threshold The prune threshold of success_hostname's decision tree
     * @param tree_prune_threshold_by_host The current prune thresholds (updated)
     * @param context Description of the situation, for error messages
     */
    void Controller::continue_from_host_output(const std::string &hostname,
                                               const std::string &success_hostname,
                                               const std::shared_ptr<SchedulingAlgorithm> &algorithm,
                                               const double prune_threshold,
                                               std::map<std::string, double> &tree_prune_threshold_by_host,
                                               const std::string &context) {
        const std::string success_host_current_task_to_schedule =
                _application_specs->get_host_task_to_schedule(success_hostname);

        algorithm->reset_host_preprocessed_decisions(hostname);

        _application_specs->update_host_running_data_size(
            hostname, _application_specs->get_host_running_data_size(success_hostname));
        _application_specs->update_host_running_error_level(
            hostname, _application_specs->get_host_running_error_level(success_hostname));
        _application_specs->update_host_task_to_schedule(hostname, success_host_current_task_to_schedule);

        if (!success_host_current_task_to_schedule.empty()) {
            _application_specs->clear_decision_tree(hostname);
            _application_specs->build_decision_tree(hostname);
            _application_specs->prune_decision_tree(hostname, prune_threshold);
            tree_prune_threshold_by_host[hostname] = prune_threshold;
            _application_specs->update_host_current_decision_node(hostname, success_hostname);
        } else {
            _application_specs->reset_host_decision_history(hostname);
            _application_specs->reset_host_current_decision_node(hostname);
        }

        terminate_running_job(hostname, context);
        _system_state_tracker->untrack_job(hostname);
        const bool is_down = _system_state_tracker->is_host_down(hostname);
        _system_state_tracker->reset_host(hostname);
        if (is_down) {
            _system_state_tracker->set_host_down(hostname);
        }
    }

    void Controller::restart_system() const {
        for (int i = 0; i < _application_specs->get_num_compute_nodes(); i++) {
            std::string hostname = "ComputeHost_" + std::to_string(i);
            WRENCH_INFO("Resetting host %s", hostname.c_str());
            if (_system_state_tracker->is_a_job_running(hostname)) {
                WRENCH_INFO("Job is running on %s, terminating it", hostname.c_str());
                try {
                    _job_manager->terminateJob(
                        _system_state_tracker->get_running_job(hostname));
                } catch (ExecutionException &) {
                    WRENCH_INFO("Tried to terminate job on down host: %s for stop running jobs hack", hostname.c_str());
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

        /* Create a job manager so that we can create/submit jobs */
        _job_manager = this->createJobManager();

        /* Create the system state tracker */
        std::vector<std::string> hostnames;
        for (auto const &entry: _compute_services) { hostnames.push_back(entry.first); }
        _system_state_tracker = SystemState::create_tracker(hostnames);

        /* Loop over all the scheduling algorithms */
        for (const auto &algorithm: _scheduling_algorithms) {
            std::cerr << "** " << algorithm->get_name().c_str() << " **" << std::endl;
            WRENCH_INFO("** Running experiments with algorithm '%s' **", algorithm->get_name().c_str());

            /* Keep track of number of successes */
            std::vector<std::pair<bool, double> > repetition_results(_num_repeats, {
                                                                         false, _application_specs->get_e_fail()
                                                                     });

            /* Do all the repeats */
            for (int repeat = 0; repeat < _num_repeats; repeat++) {
                run_repetition(algorithm, repeat, repetition_results);
            }

            /* Print the results */
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

            std::cout << std::endl;
            std::cout << "Total repeats: " << _num_repeats << "\n";
            std::cout << "Num successes: " << num_successes << "\n";
            std::cout << "Success rate: " << static_cast<double>(num_successes) / static_cast<double>(_num_repeats) <<
                    "\n";
            std::cout << "Avg error level: " << cumulative_error_level / static_cast<double>(_num_repeats) << "\n";
            if (num_successes > 0) {
                std::cout << "Avg error level of successes: " << cumulative_error_level_successes / static_cast<double>(
                    num_successes) << "\n\n";
            } else {
                std::cout << "Avg error level of successes: N/A\n\n";
            }
            for (int repeat = 0; repeat < _num_repeats; repeat++) {
                std::cout << "Repetition " << repeat << ": " << repetition_results[repeat].second << std::endl;
            }
            std::cout << std::endl;

            std::cout << "FINAL RESULTS PER REPETITION:\n";
            std::map<double, unsigned long> final_results_per_repetition;
            for (int repeat = 0; repeat < _num_repeats; repeat++) {
                double error = repetition_results[repeat].second;
                if (final_results_per_repetition.find(error) == final_results_per_repetition.end()) {
                    final_results_per_repetition[error] = 1;
                } else {
                    final_results_per_repetition[error]++;
                }
            }
            for (const auto &entry: final_results_per_repetition) {
                std::cout << entry.first << " " << entry.second << "\n";
            }
        }
        return 0;
    }

    /**
     * @brief Run one repetition, i.e., one application execution from time zero until the deadline
     *        (or until no better error level can be achieved)
     * @param algorithm The scheduling algorithm
     * @param repeat The repetition index
     * @param repetition_results The results of all repetitions (updated)
     */
    void Controller::run_repetition(const std::shared_ptr<SchedulingAlgorithm> &algorithm,
                                    const int repeat,
                                    std::vector<std::pair<bool, double> > &repetition_results) {
        RepetitionState state;
        state.repeat = repeat;

        state.start_date = Simulation::getCurrentSimulatedDate();
        std::cout << "Repetition " << repeat << std::endl;

        /* (Re-)Create node on/off turners, resetting the seed at every experiment start */
        NodeKiller::start_node_killers(this->getSimulation(),
                                       _compute_services,
                                       _application_specs->get_seed(),
                                       (repeat == 0),
                                       _application_specs->get_lambda(),
                                       _application_specs->get_restart_overhead(),
                                       this->getCommPort());

        /* Create an alarm for the deadline */
        state.deadline_date = Simulation::getCurrentSimulatedDate() + _application_specs->get_deadline();
        // WRENCH_INFO("Setting an alarm for repeat %d at time %lf", repeat, execution_deadline);
        this->setTimer(state.deadline_date, "time_out:" + algorithm->get_name() + "-" + std::to_string(repeat));

        /* Error level that each host's decision tree has been pruned to improve on (temporal redundancy) */
        for (auto &entry: _compute_services) {
            state.tree_prune_threshold_by_host[entry.first] = std::numeric_limits<double>::infinity();
        }

        /* Current task for all hosts is the first task */
        for (auto const &entry: _compute_services) {
            _application_specs->update_host_task_to_schedule(entry.first, 0);
            _application_specs->update_host_running_data_size(entry.first, _application_specs->get_initial_data_size());
            _application_specs->update_host_running_error_level(entry.first,
                                                                _application_specs->get_initial_error_level());
        }

        /* Build/reset decision trees to use for temporal redundancy */
        if (!_keep_preprocessed_decisions || repeat == 0) {
            _application_specs->clear_decision_trees();
            _application_specs->build_decision_trees();
            algorithm->reset_all_preprocessed_decisions();
        }

        _application_specs->reset_all_hosts_current_decision_nodes();
        _application_specs->reset_all_hosts_decision_history();

        _system_state_tracker->reset_all_hosts();

        /* Loop until the repetition is over */
        while (true) {
            // Invoke the scheduler
            auto decisions =
                    algorithm->make_decisions(_system_state_tracker.get(),
                                              state.deadline_date - Simulation::getCurrentSimulatedDate());

            // Implement the scheduling decisions
            for (const auto &[hostname, task, execution_option]: decisions) {
                WRENCH_INFO("Scheduling decision: run task %s with option %s on host %s at time %lf", task.c_str(),
                            execution_option.c_str(), hostname.c_str(), Simulation::getCurrentSimulatedDate() - state.start_date);
                this->submit_job(task,
                                 execution_option,
                                 _application_specs->get_host_running_data_size(hostname),
                                 _application_specs->get_host_running_error_level(hostname),
                                 hostname);
            }

            auto event = this->waitForNextEvent();
            bool repetition_is_over = false;
            if (auto success_event = std::dynamic_pointer_cast<CompoundJobCompletedEvent>(event)) {
                repetition_is_over = process_job_completion(success_event, algorithm, state, repetition_results);
            } else if (auto timer_event = std::dynamic_pointer_cast<TimerEvent>(event)) {
                repetition_is_over = process_timer_event(timer_event, algorithm, state, repetition_results);
            }
            if (repetition_is_over) {
                break;
            }
        }

        // Cancel all pending jobs as we're done
        for (const auto &entry: _compute_services) {
            std::string hostname = entry.first;
            terminate_running_job(hostname, "at the end of the repetition");
        }
    }

    /**
     * @brief Process the completion of a job: move the host on to its next task, apply reactive
     *        rescheduling, and, if the host completed the last task, temporal redundancy
     * @return true if the repetition is over
     */
    bool Controller::process_job_completion(const std::shared_ptr<CompoundJobCompletedEvent> &success_event,
                                            const std::shared_ptr<SchedulingAlgorithm> &algorithm,
                                            RepetitionState &state,
                                            std::vector<std::pair<bool, double> > &repetition_results) {
        const int repeat = state.repeat;
        const double repeat_start_date = state.start_date;

        auto success_hostname = success_event->compute_service->getHosts().at(0);

        auto job_task_name = success_event->job->getName();
        auto success_host_completed_task_name = _system_state_tracker->get_host_current_task(
            success_hostname);

        // checks if the task completed in the event is the same as the current task the host is working on
        // if its different, we know the host in question has already been reset and rescheduled
        // if its the same, then we have to reset the host
        if (job_task_name.compare(0, success_host_completed_task_name.length(),
                                  success_host_completed_task_name)) {
            // spurious timeout or whatever its called
            return false;
        }

        // checks if the task completed in the event is the same as the current task the host SHOULD have been working on
        // if its different, then we have to reset the host to make sure it gets rescheduled to the proper task
        // if its the same, then we know it completed what it set out to do successfully
        std::string success_host_current_task_to_schedule = _application_specs->
                get_host_task_to_schedule(success_hostname);
        if (job_task_name.compare(0, success_host_current_task_to_schedule.length(),
                                  success_host_current_task_to_schedule)) {
            _system_state_tracker->reset_host(success_hostname);
            _system_state_tracker->untrack_job(success_hostname);
            return false;
        }
        std::cout << job_task_name << " completed successfully by host " << success_hostname << " at "
                << (Simulation::getCurrentSimulatedDate() - repeat_start_date) << std::endl;

        std::string success_host_selected_option = job_task_name.substr(
            success_host_completed_task_name.length() + 1);
        double success_host_running_data_size = _application_specs->get_host_running_data_size(
            success_hostname);
        double success_host_running_error_level = _application_specs->get_host_running_error_level(
            success_hostname);

        // update data size and error level trackers
        std::cerr << "success_host_completed_task_name = " << success_host_completed_task_name << "\n";
        std::cerr << "Completed task name: " << success_host_completed_task_name << std::endl;
        std::cerr << "Completed selected option: " << success_host_selected_option << std::endl;
        _application_specs->update_host_running_data_size(
            success_hostname,
            _task_functions.at(success_host_completed_task_name).at(success_host_selected_option).
            at("d_function")(success_host_running_data_size,
                             success_host_running_error_level));
        _application_specs->update_host_running_error_level(
            success_hostname,
            _task_functions.at(success_host_completed_task_name).at(success_host_selected_option).
            at("e_function")(success_host_running_data_size,
                             success_host_running_error_level));
        _application_specs->increment_host_task_to_schedule(success_hostname);
        _application_specs->increment_host_current_decision_node(
            success_hostname, success_host_completed_task_name, success_host_selected_option);

        // update local trackers
        success_host_running_error_level = _application_specs->get_host_running_error_level(
            success_hostname);

        /* Reactive rescheduling (Section VI-C) */
        auto &tree_prune_threshold_by_host = state.tree_prune_threshold_by_host;
        const double success_host_tree_prune_threshold = tree_prune_threshold_by_host.at(success_hostname);

        if (_stop_running_jobs == "aggressive") {
            for (const auto &entry: _compute_services) {
                std::string hostname = entry.first;

                if (hostname != success_hostname &&
                    !_system_state_tracker->is_host_finished(hostname)) {
                    continue_from_host_output(hostname, success_hostname, algorithm,
                                              success_host_tree_prune_threshold,
                                              tree_prune_threshold_by_host, "with SRJ AGG");
                }
            }
        } else if (_stop_running_jobs == "variant") {
            for (const auto &entry: _compute_services) {
                std::string hostname = entry.first;

                if (hostname != success_hostname) {
                    if (!_application_specs->cannot_do_worse(
                            hostname, success_hostname) &&
                        !_system_state_tracker->is_host_finished(hostname)) {
                        continue_from_host_output(hostname, success_hostname, algorithm,
                                                  success_host_tree_prune_threshold,
                                                  tree_prune_threshold_by_host, "with SRJ VAR");
                    }
                }
            }
        }

        if (_application_specs->get_host_task_to_schedule(success_hostname).empty()) {
            double final_error_level = success_host_running_error_level;

            if (repetition_results[repeat].second > final_error_level) {
                repetition_results[repeat] = {true, final_error_level};
            }

            std::cout
                    << "Host "
                    << success_hostname
                    << " succeeded with error: "
                    << final_error_level
                    << std::endl;

            apply_temporal_redundancy(success_hostname, final_error_level, algorithm, state);

            if (_application_specs->decision_tree_empty(success_hostname)) {
                std::cout
                        << "Best possible error level achieved."
                        << std::endl;

                if (!_keep_preprocessed_decisions) {
                    algorithm->reset_all_preprocessed_decisions();
                }

                _system_state_tracker->reset_all_hosts();
                return true;
            }

            if (_system_state_tracker->are_all_hosts_finished()) {
                std::cout
                        << "All hosts finished."
                        << std::endl;

                if (!_keep_preprocessed_decisions) {
                    algorithm->reset_all_preprocessed_decisions();
                }

                _system_state_tracker->reset_all_hosts();
                return true;
            }
        } else {
            terminate_running_job(success_hostname, "after task success");

            _system_state_tracker->untrack_job(success_hostname);
            _system_state_tracker->reset_host(success_hostname);
        }
        return false;
    }

    /**
     * @brief Temporal redundancy (Section VI-B): after success_hostname completed the last task with the
     *        given error level, hosts may restart the application to try to achieve a lower error level
     */
    void Controller::apply_temporal_redundancy(const std::string &success_hostname,
                                               const double final_error_level,
                                               const std::shared_ptr<SchedulingAlgorithm> &algorithm,
                                               RepetitionState &state) {
        auto &tree_prune_threshold_by_host = state.tree_prune_threshold_by_host;
        const double initial_data_size = _application_specs->get_initial_data_size();
        const double initial_error_level = _application_specs->get_initial_error_level();

        if (_temporal_redundancy == "aggressive") {
            for (const auto &entry: _compute_services) {
                std::string hostname = entry.first;

                if (hostname != success_hostname) {
                    restart_host_from_first_task(hostname, algorithm, final_error_level,
                                                 tree_prune_threshold_by_host, true,
                                                 "after finish with TR AGG");
                }
            }

            if (!_system_state_tracker->is_host_finished(success_hostname)) {
                restart_host_from_first_task(success_hostname, algorithm, final_error_level,
                                             tree_prune_threshold_by_host, false, "");
            }
        } else if (_temporal_redundancy == "dependent") {
            for (const auto &entry: _compute_services) {
                std::string hostname = entry.first;

                if ((_application_specs
                     ->get_host_task_to_schedule(hostname)
                     .empty() ||
                     !_application_specs->can_possibly_do_better(
                         hostname, success_hostname)) &&
                    !_system_state_tracker->is_host_finished(hostname) &&
                    hostname != success_hostname) {
                    restart_host_from_first_task(hostname, algorithm, final_error_level,
                                                 tree_prune_threshold_by_host, true,
                                                 "after finish with TR DEP");
                }
            }

            if (!_system_state_tracker->is_host_finished(success_hostname)) {
                restart_host_from_first_task(success_hostname, algorithm, final_error_level,
                                             tree_prune_threshold_by_host, false, "");
            }
        } else if (_temporal_redundancy == "independent") {
            for (const auto &entry: _compute_services) {
                std::string hostname = entry.first;

                if (_application_specs
                    ->get_host_task_to_schedule(hostname)
                    .empty() &&
                    !_system_state_tracker->is_host_finished(hostname)) {
                    restart_host_from_first_task(hostname, algorithm, final_error_level,
                                                 tree_prune_threshold_by_host, true,
                                                 "after finish with TR INDEP");
                }
            }
        } else {
            for (const auto &entry: _compute_services) {
                std::string hostname = entry.first;

                if (_application_specs
                    ->get_host_task_to_schedule(hostname)
                    .empty() &&
                    !_system_state_tracker->is_host_finished(hostname)) {
                    if (!_keep_preprocessed_decisions) {
                        algorithm->reset_host_preprocessed_decisions(hostname);

                        _application_specs->clear_decision_tree(hostname);
                        _application_specs->build_decision_tree(hostname);
                        _application_specs->prune_decision_tree(
                            hostname, final_error_level);

                        tree_prune_threshold_by_host[hostname] =
                                final_error_level;
                    }

                    _application_specs->update_host_running_data_size(
                        hostname, initial_data_size);

                    _application_specs->update_host_running_error_level(
                        hostname, initial_error_level);

                    _application_specs->update_host_task_to_schedule(
                        hostname, 0);

                    _application_specs->reset_host_current_decision_node(hostname);
                    _application_specs->reset_host_decision_history(hostname);

                    std::cerr
                            << "Stopping host "
                            << hostname
                            << std::endl;

                    terminate_running_job(hostname, "after finish with TR OFF");

                    _system_state_tracker->untrack_job(hostname);
                    NodeKiller::stop_node_killer(hostname);
                    _system_state_tracker->reset_host(hostname);
                    _system_state_tracker->set_host_down(hostname);
                    _system_state_tracker->set_host_finished(hostname);
                }
            }
        }
    }

    /**
     * @brief Process a timer event: the deadline, or a host going down or coming back up
     * @return true if the repetition is over
     */
    bool Controller::process_timer_event(const std::shared_ptr<TimerEvent> &timer_event,
                                         const std::shared_ptr<SchedulingAlgorithm> &algorithm,
                                         const RepetitionState &state,
                                         std::vector<std::pair<bool, double> > &repetition_results) {
        const int repeat = state.repeat;
        const double repeat_start_date = state.start_date;

        // This is the catch-all timer-based stuff
        std::string timeout_prefix = "time_out";
        std::string hostup_prefix = "host_up";
        std::string hostdown_prefix = "host_down";

        // Is it a timeout?
        if (timer_event->message.compare(0, timeout_prefix.length(), timeout_prefix) == 0) {
            // Check if the colon and hyphen exist
            size_t colon_pos = timer_event->message.find(':');
            size_t hyphen_pos = timer_event->message.find('-');

            std::string algorithm_name = timer_event->message.substr(
                colon_pos + 1, hyphen_pos - colon_pos - 1);
            std::string repeat_id = timer_event->message.substr(hyphen_pos + 1);
            if (algorithm_name != algorithm->get_name() || repeat_id != std::to_string(repeat)) {
                return false; // Spurious timeout
            }
            if (!repetition_results[repeat].first) {
                std::cout << "REPETITION " << std::to_string(repeat) << " HAS FAILED (after " <<
                        Simulation::getCurrentSimulatedDate() - repeat_start_date << " seconds)" <<
                        std::endl;
            }
            WRENCH_INFO("Deadline reached");
            std::cout << "Error: " << repetition_results[repeat].second << std::endl;

            if (!_keep_preprocessed_decisions) {
                algorithm->reset_all_preprocessed_decisions();
            }
            this->restart_system();
            return true;
        }

        if (timer_event->message.compare(0, hostup_prefix.length(), hostup_prefix) == 0) {
            size_t pos = timer_event->message.find(':');
            std::string hostname = timer_event->message.substr(pos + 1);
            // Reset the host's entry to nullptr, so that we now know it's idle
            std::cerr << "Host " << hostname.c_str() << " is back up at time " <<
                    Simulation::getCurrentSimulatedDate() - repeat_start_date << std::endl;

            WRENCH_INFO("Was notified that %s is up again", hostname.c_str());
            _system_state_tracker->set_host_up(hostname);
            return false;
        }

        if (timer_event->message.compare(0, hostdown_prefix.length(), hostdown_prefix) == 0) {
            size_t pos = timer_event->message.find(':');
            std::string hostname = timer_event->message.substr(pos + 1);
            std::cerr << "Host " << hostname.c_str() << " is down at time " <<
                    Simulation::getCurrentSimulatedDate() - repeat_start_date << std::endl;

            // Cancel the job
            WRENCH_INFO("Was notified that %s is down... terminating job of need be", hostname.c_str());
            terminate_running_job(hostname, "for host going down");

            // Could any of these functions be combined?
            _system_state_tracker->untrack_job(hostname);
            _system_state_tracker->reset_host(hostname);
            _system_state_tracker->set_host_down(hostname);
        }
        return false;
    }

} // namespace wrench
