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

#include "Controller.h"
#include "NodeKiller.h"
#include "ProbabilityComputation.h"

WRENCH_LOG_CATEGORY(controller, "Log category for Controller");

namespace wrench {
    /**
     * @brief Constructor
     *
     * @param failure_spec: failure specifications
     * @param application_spec: application specifications
     * @param num_repeats: number of repeats
     * @param compute_services: a set of compute services available to run actions
     * @param storage_service: the storage service
     * @param hostname: the name of the host on which to start the Execution Controller
     */
    Controller::Controller(const boost::json::object& failure_spec,
                           const boost::json::object& application_spec,
                           const long num_repeats,
                           const std::map<std::string, std::shared_ptr<BareMetalComputeService>>& compute_services,
                           const std::shared_ptr<SimpleStorageService>& storage_service,
                           const std::string& hostname) : ExecutionController(hostname, "controller"),
                                                          _failure_spec(failure_spec),
                                                          _application_spec(application_spec),
                                                          _num_repeats(num_repeats),
                                                          _compute_services(compute_services),
                                                          _storage_service(storage_service) {
        _deadline = boost::json::value_to<double>(_application_spec.at("deadline"));
        _lambda = boost::json::value_to<double>(_failure_spec.at("lambda"));
        _exponential_distribution = std::exponential_distribution<double>(_lambda);
        _seed = boost::json::value_to<int>(_failure_spec.at("seed"));
        if (_seed < 0) {
            _seed = static_cast<int>(std::chrono::system_clock::now().time_since_epoch().count());
        }
    }

    /**
     * @brief Method to start a node killer on a host
     * @param victim: the victim's hostname
     * @param seed: the seed for the RNG
     * @return A node killer service
     */
    std::shared_ptr<NodeKiller> Controller::start_node_killer(const std::string& victim, const int seed) {
        // Turn the host (back) on just in case
        Simulation::turnOnHost(victim);

        // Start the NodeKiller service
        auto restart_overhead = boost::json::value_to<double>(_failure_spec.at("restart_overhead"));
        auto murderer = std::make_shared<NodeKiller>(
            std::default_random_engine(seed),
            _exponential_distribution,
            restart_overhead,
            victim, this->commport,
            "ControllerHost");
        murderer->setSimulation(this->getSimulation());
        murderer->start(murderer, true, false); // Daemonized, no auto-restart
        return murderer;
    }


    /**
     * @brief Start a node killer on each host
     */
    void Controller::start_node_killers() {
        static int seed = _seed;
        for (int i = 0; i < _compute_services.size(); i++) {
            auto victim_hostname = "ComputeHost_" + std::to_string(i);
            // Kill an existing node killer if any
            if (_node_killers.find(victim_hostname) != _node_killers.end()) {
                _node_killers[victim_hostname]->killActor(); // brutal
            }
            // Start a node killer (note the seed++ there)
            _node_killers[victim_hostname] = this->start_node_killer(victim_hostname, seed++);
        }
    }


    /**
     * @brief main method of the Controller
     *
     * @return 0 on completion
     *
     */
    int Controller::main() {
        /* Set the logging output to GREEN */
        TerminalOutput::setThisProcessLoggingColor(TerminalOutput::COLOR_GREEN);
        WRENCH_INFO("Controller starting");

        /* Create a job manager so that we can create/submit jobs */
        auto job_manager = this->createJobManager();

        /* Calculate estimate deltat probability to compare to */
        auto restart_overhead = boost::json::value_to<double>(_failure_spec.at("restart_overhead"));
        auto task_time = boost::json::value_to<double>(_application_spec.at("tasks").as_array()[0].as_object().at("exec_time"));
        auto prob = std::make_unique<ProbabilityComputation>(_lambda, restart_overhead);

#if 0
        double deltat_computation = prob->compute_best_deltat(task_time, _deadline, 1e-3);
        prob->set_delta_t(deltat_computation);


        double probability_upper_bound = prob->compute_probability(task_time, _deadline, false);
        double probability_lower_bound = prob->compute_probability(task_time, _deadline, true);
        double probability_midpoint = (probability_upper_bound + probability_lower_bound) / 2;
#endif
        double deltat_computation = prob->compute_best_deltat(task_time, _deadline, 1e-2);
        prob->set_delta_t(deltat_computation);

        // std::cout << "TASK TIME = " << task_time << "\n";

        double probability_upper_bound = prob->compute_probability(task_time, _deadline, false);
        double probability_lower_bound = prob->compute_probability(task_time, _deadline, true);
	double probability_midpoint = (probability_upper_bound + probability_lower_bound) / 2;

        /* Keep track of number of successes */
        int num_successes = 0;

        /* Do all the repeats */
        for (int repeat = 0; repeat < _num_repeats; repeat++) {
            /* (Re-)Create node on/off turners */
            start_node_killers();

            /* Create an alarm for the deadline */
            WRENCH_INFO("Setting an alarm for repeat %d at time %lf", repeat, Simulation::getCurrentSimulatedDate() + _deadline);
            this->setTimer(Simulation::getCurrentSimulatedDate() + _deadline, "time_out:" + std::to_string(repeat));

            /* Create the map of hosts, where entries are either null (if idle) or
             * a submitted job
             */
            std::map<std::string, std::shared_ptr<CompoundJob>> running_jobs;
            for (const auto &item : _compute_services) {
                running_jobs[item.first] = nullptr;;
            }

            /* Loop until the task completes successfully somewhere */
            bool success = false;
            while (true) {
                // Submit the task to each idle hosts
                for (const auto& [hostname, job] : running_jobs) {
                    if (job == nullptr) {
                        auto new_job = job_manager->createCompoundJob("");
                        new_job->addSleepAction("", task_time);
                        WRENCH_INFO("Submitting a new job to %s", hostname.c_str());
                        job_manager->submitJob(new_job, _compute_services.at(hostname));
                        running_jobs[hostname] = new_job;
                    }
                }

                // Here we could instead call waitForAndProcessNextEvent() and define the handling
                // methods, in case this if-else-if thing becomes too unwieldly
                auto event = this->waitForNextEvent();
                if (auto success_event = std::dynamic_pointer_cast<CompoundJobCompletedEvent>(event)) {
                    auto hostname = success_event->compute_service->getHosts().at(0);
                    WRENCH_INFO("A job succeeded on %s... we're done!", hostname.c_str());
                    success = true;
                    num_successes++;
                    break;
                } else if (auto timer_event = std::dynamic_pointer_cast<TimerEvent>(event)) {
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
                        std::cout << "REPETITION " << std::to_string(repeat) << " HAS FAILED (time:" << Simulation::getCurrentSimulatedDate() << ")" << std::endl;
                        WRENCH_INFO("Deadline reached :(");
                        break;
                    }

                    if (timer_event->message.compare(0, hostup_prefix.length(), hostup_prefix) == 0) {
                        size_t pos = timer_event->message.find(':');
                        std::string hostname = timer_event->message.substr(pos + 1);
                        // Reset the host's entry to nullptr, so that we now know it's idle
                        WRENCH_INFO("Was notified that %s is up again", hostname.c_str());
                        running_jobs[hostname] = nullptr;
                        continue;
                    }

                    if (timer_event->message.compare(0, hostdown_prefix.length(), hostdown_prefix) == 0) {
                        size_t pos = timer_event->message.find(':');
                        std::string hostname = timer_event->message.substr(pos + 1);
                        // Cancel the job
                        WRENCH_INFO("Was notified that %s is down... terminating job", hostname.c_str());
                        job_manager->terminateJob(running_jobs[hostname]);
                        // Leave the job in the map, so that we don't mistake the host as idle
                        continue;
                    }
                }
            }
            // Cancel pending jobs
            for (const auto& [hostname, job] : running_jobs) {
                if (job) {
                    try {
                        job_manager->terminateJob(job);
                    } catch (ExecutionException &ignore) {}
                }
            }
            // std::cout << "REPEAT " << repeat << ": " << (success ? "SUCCESS" : "FAILURE") << std::endl;
        }

#if 0
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

        return 0;
    }
} // namespace wrench
