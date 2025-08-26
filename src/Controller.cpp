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

// #define COMPUTE_PROBABILITIES 1

#include <iostream>
#include <wrench/util/UnitParser.h>

#include "Controller.h"
#include "FunctionGenerator.h"
#include "NodeKiller.h"
#include "ProbabilityComputation.h"
#include "RunningJob.h"
#include "JobTracker.h"
#include "SchedulingAlgorithm.h"

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
    Controller::Controller(const boost::json::object& platform_spec,
                           const boost::json::object& failure_spec,
                           const boost::json::object& application_spec,
                           const boost::json::object& execution_spec,
                           const boost::json::object& scheduling_spec,
                           const std::map<std::string, std::shared_ptr<BareMetalComputeService>>& compute_services,
                           const std::shared_ptr<SimpleStorageService>& storage_service,
                           const std::string& hostname) : ExecutionController(hostname, "controller"),
                                                          _platform_spec(platform_spec),
                                                          _failure_spec(failure_spec),
                                                          _application_spec(application_spec),
                                                          _execution_spec(execution_spec),
                                                          _scheduling_spec(scheduling_spec),
                                                          _compute_services(compute_services),
                                                          _storage_service(storage_service) {
        _io_read_bandwidth = wrench::UnitParser::parse_bandwidth(
            boost::json::value_to<string>(_platform_spec.at("io_read_bandwidth")));
        _io_write_bandwidth = wrench::UnitParser::parse_bandwidth(
            boost::json::value_to<string>(_platform_spec.at("io_write_bandwidth")));
        _num_repeats = boost::json::value_to<long>(_execution_spec.at("num_repeats"));
        _deadline = boost::json::value_to<double>(_execution_spec.at("deadline"));
        _restart_overhead = boost::json::value_to<double>(_failure_spec.at("restart_overhead"));
        _e_fail = boost::json::value_to<double>(_execution_spec.at("e_fail"));
        _lambda = boost::json::value_to<double>(_failure_spec.at("lambda"));
        _exponential_distribution = std::exponential_distribution<double>(_lambda);
        _seed = boost::json::value_to<int>(_failure_spec.at("seed"));
        _delta_t = boost::json::value_to<double>(_scheduling_spec.at("delta_t"));
        _delta_t_precision = boost::json::value_to<double>(_scheduling_spec.at("delta_t_precision"));

        if (_seed < 0) {
            _seed = static_cast<int>(std::chrono::system_clock::now().time_since_epoch().count());
        }

        /* Create the data structure that describes all task functions */
        for (const auto& task : _application_spec.at("tasks").as_array()) {
            auto task_name = boost::json::value_to<std::string>(task.as_object().at("name"));
            auto& exec_options = task.as_object().at("execution_options").as_array();

            for (const auto& option : exec_options) {
                auto option_name = boost::json::value_to<std::string>(option.as_object().at("name"));

                for (auto function_name : {"t_function", "d_function", "e_function"}) {
                    auto& function = option.as_object().at(function_name).as_object();
                    _task_functions[task_name][option_name][function_name] = FunctionGenerator::get_function(function);
                }
            }
        }

        /* Determine the list of scheduling algorithms */
        /* Determine/validate the scheduling algorithms to use */
        std::string scheduling_type;
        if (_application_spec.at("tasks").as_array().size() == 1) {
            scheduling_type = "one_task";
        }
        else {
            throw std::invalid_argument("Multi-task applications not supported (yet)");
        }
        for (auto const& alg_name : _scheduling_spec.at("algorithms").at(scheduling_type).as_array()) {
            auto alg = SchedulingAlgorithm::create_scheduling_algorithm(
                            boost::json::value_to<string>(alg_name),
                            _e_fail, _delta_t, _delta_t_precision,
                            _restart_overhead, _io_read_bandwidth, _io_write_bandwidth);

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
        auto job = _job_manager->createCompoundJob(task_name + "_" + execution_option);

        auto read_input_action = job->addCustomAction("read", 0, 1,
                                                      [this, running_output_data_size](
                                                      const std::shared_ptr<wrench::ActionExecutor>& action_executor) {
                                                          _storage_disk->write(
                                                              static_cast<sg_size_t>(running_output_data_size));
                                                      },
                                                      [](const std::shared_ptr<wrench::ActionExecutor>&
                                                      action_executor) {
                                                      });

        auto compute_action = job->addComputeAction("compute",
                                                    _task_functions[task_name][execution_option]["t_function"]
                                                    (running_output_data_size, running_output_error_level),
                                                    0.0,
                                                    1, 1, ParallelModel::CONSTANTEFFICIENCY(1.0));

        auto write_output_action = job->addCustomAction("write", 0, 1,
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
        _probability_computation = std::make_unique<ProbabilityComputation>(_lambda, _restart_overhead);

        /* Get initial x (data size) and y (error) from the JSON file */
        auto initial_data_size = boost::json::value_to<double>(_application_spec.at("initial_data_size"));
        auto initial_error_level = boost::json::value_to<double>(_application_spec.at("initial_error_level"));

#ifdef COMPUTE_PROBABILITIES
        double deltat_computation = prob->compute_best_deltat(task_time, _deadline, 1e-3);
        prob->set_delta_t(deltat_computation);
        double probability_upper_bound = prob->compute_probability(task_time, _deadline, false);
        double probability_lower_bound = prob->compute_probability(task_time, _deadline, true);
        double probability_midpoint = (probability_upper_bound + probability_lower_bound) / 2;
#endif

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
                                               _seed,
                                               (repeat == 0),
                                               _exponential_distribution,
                                               _restart_overhead,
                                               this->commport);

                /* Create an alarm for the deadline */
                auto time_to_deadline = Simulation::getCurrentSimulatedDate() + _deadline;
                // WRENCH_INFO("Setting an alarm for repeat %d at time %lf", repeat, execution_deadline);
                this->setTimer(time_to_deadline, "time_out:" + std::to_string(repeat));

                /* Create the job tracker */
                std::vector<std::string> hostnames;
                for (auto const &entry : _compute_services) { hostnames.push_back(entry.first); }
                auto job_tracker = JobTracker::create_tracker(hostnames);

                /* Running values of output data size and error level */
                auto running_output_data_size = initial_data_size;
                auto running_output_error_level = initial_error_level;

                /* Current task is the first task */
                auto current_task = std::string(_application_spec.at("tasks").as_array().
                                                                  at(0).as_object().at("name").as_string().c_str());

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
                           time_to_deadline - Simulation::getCurrentSimulatedDate());

                    // Implement the scheduling decisions
                    for (auto const &decision : decisions) {
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
                        std::cout << "REPETITION " << std::to_string(repeat) << " HAS SUCCEEDED (after " <<
                            Simulation::getCurrentSimulatedDate()  - repeat_start_date << " seconds)" << std::endl;
                        num_successes++;
                        /* TODO: With multiple tasks, we would want to proceed to the next one here, as well as cancel all the rest
                         * Realistically, should this be done with a forced restart of the other hosts?
                         * Or would the other hosts be able to start on the new task and give up the old one instantly?
                         * We also need to update the running input data size and input error level based on the
                         * execution option that was successful
                         */
                        break;
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
                            std::cout << "REPETITION " << std::to_string(repeat) << " HAS FAILED (after " <<
                                Simulation::getCurrentSimulatedDate() - repeat_start_date << " seconds)" << std::endl;
                            WRENCH_INFO("Deadline reached :(");
                            break;
                        }

                        if (timer_event->message.compare(0, hostup_prefix.length(), hostup_prefix) == 0) {
                            size_t pos = timer_event->message.find(':');
                            std::string hostname = timer_event->message.substr(pos + 1);
                            // Reset the host's entry to nullptr, so that we now know it's idle
                            WRENCH_INFO("Was notified that %s is up again", hostname.c_str());
                            job_tracker->untrack_job(hostname);
                            continue;
                        }

                        if (timer_event->message.compare(0, hostdown_prefix.length(), hostdown_prefix) == 0) {
                            size_t pos = timer_event->message.find(':');
                            std::string hostname = timer_event->message.substr(pos + 1);
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

#ifdef COMPUTE_PROBABILITIES
        double experimental_probability = static_cast<double>(num_successes) / static_cast<double>(_num_repeats);
        double relative_error = std::abs(probability_midpoint - experimental_probability) /
                                std::max(std::abs(probability_midpoint), std::abs(experimental_probability));

        std::cout << std::endl << "TOTAL REPEATS = " << _num_repeats << "    TOTAL SUCCESSES = " << num_successes <<
                std::endl;
        std::cout << "EXPERIMENTAL PROBABILITY = " << experimental_probability << std::endl;
        std::cout << "DELTA PROBABILITY = " << probability_midpoint << "    with deltat = " << prob->get_delta_t() <<
                std::endl;
        std::cout << "IS THIS ACCURATE ENOUGH? " << (relative_error < 1e-2 ? "YES" : "NO") << std::endl;
#endif

        return
            0;
    }
} // namespace wrench
