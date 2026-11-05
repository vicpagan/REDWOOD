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
#include "Discretization.h"
#include "SystemState.h"
#include "scheduling/SchedulingAlgorithm.h"

WRENCH_LOG_CATEGORY(controller, "Log category for Controller");

namespace wrench {
    /**
     * @brief Constructor: reads the number of repetitions and the temporal redundancy / reactive rescheduling
     *        settings, creates the discretization and creates the scheduling algorithms listed in the specification
     * @param application_spec The "application" part of the JSON input
     * @param execution_spec The "execution" part of the JSON input (num_repeats, ...)
     * @param scheduling_spec The "scheduling" part of the JSON input (algorithms, hacks, ...)
     * @param application_specs The parsed application specification (also holds the per-host runtime state)
     * @param compute_services The compute service of each compute host, by hostname
     * @param fake_io Whether to simulate I/O as sleeps instead of disk operations
     * @param hostname The name of the host on which to start the Controller
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
        _reactive_rescheduling = boost::json::value_to<std::string>(
            _scheduling_spec.at("hacks").as_object().at("reactive_rescheduling"));
        _keep_preprocessed_decisions = (_temporal_redundancy == "off" && _reactive_rescheduling == "off");

        // create the discretization utility
        _discretization = std::make_unique<Discretization>(_application_specs);

        // determine the list of scheduling algorithms
        for (auto const &alg_name: _scheduling_spec.at("algorithms").as_array()) {
            auto alg = SchedulingAlgorithm::create_scheduling_algorithm(
                boost::json::value_to<string>(alg_name), _application_specs, _task_functions,
                _discretization.get());

            _scheduling_algorithms.push_back(alg);
        }
    }

    /**
     * @brief Create a job that executes a task with an execution option (read input, compute, write output)
     *        on a host, submit it, and track it in the system state
     * @param task_name The task name
     * @param execution_option The execution option
     * @param running_output_data_size The input data size of the task (the output size of the previous task)
     * @param running_output_error_level The input error level of the task (the output error of the previous task)
     * @param hostname The host on which to run the job
     */
    void Controller::submit_job(const std::string &task_name,
                                const std::string &execution_option,
                                double running_output_data_size,
                                double running_output_error_level,
                                const std::string &hostname) {
        const auto job = _job_manager->createCompoundJob(task_name + "_" + execution_option);

        // WRENCH_INFO("Submitting job for task %s with execution option %s on host %s", task_name.c_str(),
                    // execution_option.c_str(), hostname.c_str());

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

            // WRENCH_INFO("Read input action for task %s with execution option %s on host %s", task_name.c_str(),
                        // execution_option.c_str(), hostname.c_str());

            const auto compute_action =
                    job->addComputeAction(
                        "compute",
                        _task_functions.at(task_name).at(execution_option).at(
                            "t_function")
                        (running_output_data_size, running_output_error_level),
                        0.0,
                        1, 1, ParallelModel::CONSTANTEFFICIENCY(1.0));

            // WRENCH_INFO("Compute action for task %s with execution option %s on host %s", task_name.c_str(),
                        // execution_option.c_str(), hostname.c_str());

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

            // WRENCH_INFO("Write output action for task %s with execution option %s on host %s", task_name.c_str(),
                        // execution_option.c_str(), hostname.c_str());

            job->addActionDependency(read_input_action, compute_action);
            job->addActionDependency(compute_action, write_output_action);
        }

        WRENCH_INFO("Submitting a new job to %s", hostname.c_str());
        _job_manager->submitJob(job, _compute_services.at(hostname));

        _system_state_tracker->track_job(job, hostname, task_name, execution_option,
                                         Simulation::getCurrentSimulatedDate());
    }

    /**
     * @brief Terminate the job running on a host, if any (a failed termination is reported on stderr)
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
     *        (temporal redundancy). The host's down status is kept.
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
        bool is_host_down = _system_state_tracker->is_host_down(hostname);
        _system_state_tracker->reset_host(hostname);
        if (is_host_down) {
            _system_state_tracker->set_host_down(hostname);
        }
    }

    /**
     * @brief Make a host abandon its execution and continue from the output just produced by
     *        success_hostname (reactive rescheduling): it takes over success_hostname's data size, error level,
     *        next task and decision history. The host's down status is kept.
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

        const std::string success_host_current_task_to_schedule = _application_specs->get_host_task_to_schedule(success_hostname);

        algorithm->reset_host_preprocessed_decisions(hostname);

        _application_specs->update_host_running_data_size(hostname, _application_specs->get_host_running_data_size(success_hostname));
        _application_specs->update_host_running_error_level(hostname, _application_specs->get_host_running_error_level(success_hostname));
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
        bool is_host_down = _system_state_tracker->is_host_down(hostname);
        _system_state_tracker->reset_host(hostname);
        if (is_host_down) {
            _system_state_tracker->set_host_down(hostname);
        }
    }

    /**
     * @brief At the deadline: terminate the job running on each host, if any, and mark those hosts as idle and up
     */
    void Controller::restart_system() const {
        for (int i = 0; i < _application_specs->get_num_compute_nodes(); i++) {
            std::string hostname = "ComputeHost_" + std::to_string(i);
            // WRENCH_INFO("Resetting host %s", hostname.c_str());
            if (_system_state_tracker->is_a_job_running(hostname)) {
                // WRENCH_INFO("Job is running on %s, terminating it", hostname.c_str());
                try {
                    _job_manager->terminateJob(
                        _system_state_tracker->get_running_job(hostname));
                } catch (ExecutionException &) {
                    // WRENCH_INFO("Tried to terminate job on down host: %s for reactive rescheduling", hostname.c_str());
                }
                _system_state_tracker->reset_host(hostname);
                _system_state_tracker->untrack_job(hostname);
                _system_state_tracker->set_host_up(hostname);
            }
        }
    }

    /**
     * @brief Main method of the Controller: for each scheduling algorithm, run all repetitions and print the
     *        results (summary, "Repetition <i>: <error>" lines, and the distribution of final error levels)
     * @return 0 on completion
     */
    int Controller::main() {
        TerminalOutput::setThisProcessLoggingColor(TerminalOutput::COLOR_GREEN);
        WRENCH_INFO("Controller starting");

        // create a job manager so that we can create/submit jobs
        _job_manager = this->createJobManager();

        // create the system state tracker
        std::vector<std::string> hostnames;
        for (auto const &entry: _compute_services) { hostnames.push_back(entry.first); }
        _system_state_tracker = SystemState::create_tracker(hostnames);

        // loop over all the scheduling algorithms
        for (const auto &algorithm: _scheduling_algorithms) {
            std::cerr << "** " << algorithm->get_name().c_str() << " **" << std::endl;
            WRENCH_INFO("** Running experiments with algorithm '%s' **", algorithm->get_name().c_str());

            // keep track of number of successes
            std::vector<std::pair<bool, double> > repetition_results(_num_repeats, {false, _application_specs->get_e_fail()});

            // do all the repeats
            for (int repeat = 0; repeat < _num_repeats; repeat++) {
                run_repetition(algorithm, repeat, repetition_results);
            }

            // print the results
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
            std::cout << "Success rate: " << static_cast<double>(num_successes) / static_cast<double>(_num_repeats) << "\n";
            std::cout << "Avg error level: " << cumulative_error_level / static_cast<double>(_num_repeats) << "\n";
            if (num_successes > 0) {
                std::cout << "Avg error level of successes: " << cumulative_error_level_successes / static_cast<double>(num_successes) << "\n\n";
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
     *        (or until no better error level can be achieved): start the node killers, set the deadline timer,
     *        reset all hosts to the first task, then alternate scheduling decisions and event processing
     * @param algorithm The scheduling algorithm
     * @param repeat The repetition index
     * @param repetition_results The (success, error level) of each repetition; this repetition's entry is updated
     */
    void Controller::run_repetition(const std::shared_ptr<SchedulingAlgorithm> &algorithm,
                                    const int repeat,
                                    std::vector<std::pair<bool, double>> &repetition_results) {
        RepetitionState state;
        state.repeat = repeat;
        state.start_date = Simulation::getCurrentSimulatedDate();
        std::cout << "Repetition " << repeat << std::endl;

        // (re-)create node on/off turners, resetting the seed at every experiment start
        NodeKiller::start_node_killers(this->getSimulation(),
                                       _compute_services,
                                       _application_specs->get_seed(),
                                       (repeat == 0),
                                       _application_specs->get_lambda(),
                                       _application_specs->get_restart_overhead(),
                                       this->getCommPort());

        // create an alarm for the deadline
        state.deadline_date = Simulation::getCurrentSimulatedDate() + _application_specs->get_deadline();
        // WRENCH_INFO("Setting an alarm for repeat %d at time %lf", repeat, execution_deadline);
        this->setTimer(state.deadline_date, "time_out:" + algorithm->get_name() + "-" + std::to_string(repeat));

        // error level that each host's decision tree has been pruned to improve on (temporal redundancy)
        for (auto &entry: _compute_services) {
            state.tree_prune_threshold_by_host[entry.first] = std::numeric_limits<double>::infinity();
        }

        // current task for all hosts is the first task
        for (auto const &entry: _compute_services) {
            _application_specs->update_host_task_to_schedule(entry.first, 0);
            _application_specs->update_host_running_data_size(entry.first, _application_specs->get_initial_data_size());
            _application_specs->update_host_running_error_level(entry.first,
                                                                _application_specs->get_initial_error_level());
        }

        // build/reset decision trees to use for temporal redundancy
        if (!_keep_preprocessed_decisions || repeat == 0) {
            _application_specs->clear_decision_trees();
            _application_specs->build_decision_trees();
            algorithm->reset_all_preprocessed_decisions();
        }

        _application_specs->reset_all_hosts_current_decision_nodes();
        _application_specs->reset_all_hosts_decision_history();

        _system_state_tracker->reset_all_hosts();

        // loop until the repetition is over (either through completion or reaching the deadline)
        bool repetition_is_over = false;
        while (!repetition_is_over) {

            // invoke the scheduler
            auto decisions = algorithm->make_decisions(_system_state_tracker.get(), state.deadline_date - Simulation::getCurrentSimulatedDate());

            // implement the scheduling decisions
            for (const auto &[hostname, task, execution_option]: decisions) {
                // WRENCH_INFO("Scheduling decision: run task %s with option %s on host %s at time %lf", task.c_str(),
                            // execution_option.c_str(), hostname.c_str(), Simulation::getCurrentSimulatedDate() - state.start_date);
                this->submit_job(task,
                                 execution_option,
                                 _application_specs->get_host_running_data_size(hostname),
                                 _application_specs->get_host_running_error_level(hostname),
                                 hostname);
            }

            auto event = this->waitForNextEvent();
            if (auto success_event = std::dynamic_pointer_cast<CompoundJobCompletedEvent>(event)) {
                repetition_is_over = process_job_completion(success_event, algorithm, state, repetition_results);
            } else if (auto timer_event = std::dynamic_pointer_cast<TimerEvent>(event)) {
                repetition_is_over = process_timer_event(timer_event, algorithm, state);
            }
        }

        // cancel all pending jobs as we're done
        for (const auto &entry: _compute_services) {
            std::string hostname = entry.first;
            terminate_running_job(hostname, "at the end of the repetition");
        }
    }

    /**
     * @brief Temporal redundancy (Section VI-B), applied after success_hostname completed the last task:
     *        depending on the "temporal_redundancy" setting (aggressive, dependent, independent), hosts restart the
     *        application from the first task with their decision tree pruned to improve on final_error_level;
     *        with "off", hosts that have completed the last task are stopped and marked finished
     * @param success_hostname The host that just completed the last task
     * @param final_error_level The error level of the output it produced
     * @param algorithm The scheduling algorithm
     * @param state The state of the repetition (its prune thresholds are updated)
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
                    restart_host_from_first_task(hostname, algorithm, final_error_level, tree_prune_threshold_by_host, true, "after finish with TR AGG");
                }
            }

            if (!_system_state_tracker->is_host_finished(success_hostname)) {
                restart_host_from_first_task(success_hostname, algorithm, final_error_level, tree_prune_threshold_by_host, false, "");
            }
        } else if (_temporal_redundancy == "dependent") {
            for (const auto &entry: _compute_services) {
                std::string hostname = entry.first;

                if ((_application_specs->get_host_task_to_schedule(hostname).empty() ||
                    !_application_specs->can_possibly_do_better(hostname, success_hostname)) &&
                    !_system_state_tracker->is_host_finished(hostname) &&
                    hostname != success_hostname) {

                    restart_host_from_first_task(hostname, algorithm, final_error_level, tree_prune_threshold_by_host, true, "after finish with TR DEP");
                }
            }

            if (!_system_state_tracker->is_host_finished(success_hostname)) {
                restart_host_from_first_task(success_hostname, algorithm, final_error_level, tree_prune_threshold_by_host, false, "");
            }
        } else if (_temporal_redundancy == "independent") {
            for (const auto &entry: _compute_services) {
                std::string hostname = entry.first;

                if (_application_specs->get_host_task_to_schedule(hostname).empty() && !_system_state_tracker->is_host_finished(hostname)) {
                    restart_host_from_first_task(hostname, algorithm, final_error_level, tree_prune_threshold_by_host, true, "after finish with TR INDEP");
                }
            }
        } else {
            for (const auto &entry: _compute_services) {
                std::string hostname = entry.first;

                if (_application_specs->get_host_task_to_schedule(hostname).empty() && !_system_state_tracker->is_host_finished(hostname)) {
                    if (!_keep_preprocessed_decisions) {
                        algorithm->reset_host_preprocessed_decisions(hostname);

                        _application_specs->clear_decision_tree(hostname);
                        _application_specs->build_decision_tree(hostname);
                        _application_specs->prune_decision_tree(hostname, final_error_level);

                        tree_prune_threshold_by_host[hostname] = final_error_level;
                    }

                    _application_specs->update_host_running_data_size(hostname, initial_data_size);
                    _application_specs->update_host_running_error_level(hostname, initial_error_level);
                    _application_specs->update_host_task_to_schedule(hostname, 0);

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
     * @brief Process the completion of a job: ignore it if it is stale, otherwise update the host's data size,
     *        error level, next task and decision node, apply reactive rescheduling (Section VI-C, the
     *        "reactive_rescheduling" setting), and, if the host completed the last task, record the result and apply
     *        temporal redundancy
     * @param success_event The job completion event
     * @param algorithm The scheduling algorithm
     * @param state The state of the repetition (its prune thresholds may be updated)
     * @param repetition_results The (success, error level) of each repetition; this repetition's entry is updated
     *        when the host completed the last task with a lower error level
     * @return true if the repetition is over (no better error level can be achieved, or all hosts are finished)
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
        if (job_task_name.compare(0, success_host_completed_task_name.length(), success_host_completed_task_name)) {
            return false;
        }

        // checks if the task completed in the event is the same as the current task the host SHOULD have been working on
        // if its different, then we have to reset the host to make sure it gets rescheduled to the proper task
        // if its the same, then we know it completed what it set out to do successfully
        std::string success_host_current_task_to_schedule = _application_specs-> get_host_task_to_schedule(success_hostname);
        if (job_task_name.compare(0, success_host_current_task_to_schedule.length(), success_host_current_task_to_schedule)) {
            _system_state_tracker->reset_host(success_hostname);
            _system_state_tracker->untrack_job(success_hostname);
            return false;
        }

        std::cout << job_task_name << " completed successfully by host " << success_hostname << " at "
                << (Simulation::getCurrentSimulatedDate() - repeat_start_date) << std::endl;

        std::string success_host_selected_option = job_task_name.substr(success_host_completed_task_name.length() + 1);
        double success_host_running_data_size = _application_specs->get_host_running_data_size(success_hostname);
        double success_host_running_error_level = _application_specs->get_host_running_error_level(success_hostname);

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
        _application_specs->increment_host_current_decision_node(success_hostname, success_host_completed_task_name, success_host_selected_option);

        // update local trackers
        success_host_running_error_level = _application_specs->get_host_running_error_level(success_hostname);

        // reactive rescheduling
        auto &tree_prune_threshold_by_host = state.tree_prune_threshold_by_host;
        const double success_host_tree_prune_threshold = tree_prune_threshold_by_host.at(success_hostname);
        if (_reactive_rescheduling == "aggressive") {
            for (const auto &entry: _compute_services) {
                std::string hostname = entry.first;

                if (hostname != success_hostname &&
                    !_system_state_tracker->is_host_finished(hostname)) {
                    continue_from_host_output(hostname, success_hostname, algorithm,
                                              success_host_tree_prune_threshold,
                                              tree_prune_threshold_by_host, "with RR AGG");
                }
            }
        } else if (_reactive_rescheduling == "variant") {
            for (const auto &entry: _compute_services) {
                std::string hostname = entry.first;

                if (hostname != success_hostname) {
                    if (!_application_specs->can_possibly_do_better(hostname, success_hostname) &&
                        !_system_state_tracker->is_host_finished(hostname)) {
                        continue_from_host_output(hostname, success_hostname, algorithm,
                                                  success_host_tree_prune_threshold,
                                                  tree_prune_threshold_by_host, "with RR VAR");
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

            if (_application_specs->decision_tree_empty(success_hostname) || _system_state_tracker->are_all_hosts_finished()) {
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
     * @brief Process a timer event: the deadline of this repetition ("time_out:<algorithm>-<repeat>"; timeouts
     *        of other algorithms or repetitions are ignored), or a host going down ("host_down:<host>", its job is
     *        terminated) or coming back up ("host_up:<host>")
     * @param timer_event The timer event
     * @param algorithm The scheduling algorithm
     * @param state The state of the repetition
     * @return true if the deadline of this repetition was reached (the repetition is over)
     */
    bool Controller::process_timer_event(const std::shared_ptr<TimerEvent> &timer_event,
                                         const std::shared_ptr<SchedulingAlgorithm> &algorithm,
                                         const RepetitionState &state) {
        const int repeat = state.repeat;

        std::string timeout_prefix = "time_out";
        std::string hostup_prefix = "host_up";
        std::string hostdown_prefix = "host_down";

        // deadline has been reached
        if (timer_event->message.compare(0, timeout_prefix.length(), timeout_prefix) == 0) {
            size_t colon_pos = timer_event->message.find(':');
            size_t hyphen_pos = timer_event->message.find('-');

            std::string algorithm_name = timer_event->message.substr(colon_pos + 1, hyphen_pos - colon_pos - 1);
            std::string repeat_id = timer_event->message.substr(hyphen_pos + 1);
            if (algorithm_name != algorithm->get_name() || repeat_id != std::to_string(repeat)) {
                return false; // spurious timeout
            }
            WRENCH_INFO("Deadline reached");

            if (!_keep_preprocessed_decisions) {
                algorithm->reset_all_preprocessed_decisions();
            }
            this->restart_system();
            return true;
        }

        // host is back up from being down
        if (timer_event->message.compare(0, hostup_prefix.length(), hostup_prefix) == 0) {
            size_t pos = timer_event->message.find(':');
            std::string hostname = timer_event->message.substr(pos + 1);

            WRENCH_INFO("Was notified that %s is up again", hostname.c_str());
            _system_state_tracker->set_host_up(hostname);
            return false;
        }

        // host is down
        if (timer_event->message.compare(0, hostdown_prefix.length(), hostdown_prefix) == 0) {
            size_t pos = timer_event->message.find(':');
            std::string hostname = timer_event->message.substr(pos + 1);

            // cancel the job
            WRENCH_INFO("Was notified that %s is down... terminating job if need be", hostname.c_str());
            terminate_running_job(hostname, "for host going down");

            // TODO: Could any of these functions be combined?
            _system_state_tracker->untrack_job(hostname);
            _system_state_tracker->reset_host(hostname);
            _system_state_tracker->set_host_down(hostname);
        }
        return false;
    }

} // namespace wrench
