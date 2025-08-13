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
#include <wrench/execution_controller/ExecutionControllerMessage.h>

#include "Controller.h"

#include "NodeKiller.h"

WRENCH_LOG_CATEGORY(controller, "Log category for Controller");

namespace wrench {
    /**
     * @brief Constructor
     *
     * @param failure_spec: failure specifications
     * @param application_spec: application specifications
     * @param compute_services: a set of compute services available to run actions
     * @param storage_service: the storage service
     * @param hostname: the name of the host on which to start the Execution Controller
     */
    Controller::Controller(const boost::json::object& failure_spec,
                           const boost::json::object& application_spec,
                           const std::map<std::string, std::shared_ptr<BareMetalComputeService>>& compute_services,
                           const std::shared_ptr<SimpleStorageService>& storage_service,
                           const std::string& hostname) : ExecutionController(hostname, "controller"),
                                                          _failure_spec(failure_spec),
                                                          _application_spec(application_spec),
                                                          _compute_services(compute_services),
                                                          _storage_service(storage_service) {
    }

    void Controller::start_node_killers() {
        std::default_random_engine rng;
        auto lambda = boost::json::value_to<double>(_failure_spec.at("lambda"));
        auto exponential_distribution = std::exponential_distribution<double>(lambda);
        int seed = boost::json::value_to<int>(_failure_spec.at("seed"));
        if (seed < 0) {
            seed = static_cast<int>(std::chrono::system_clock::now().time_since_epoch().count());
        }
        auto restart_overhead = boost::json::value_to<double>(_failure_spec.at("restart_overhead"));
        for (int i = 0; i < _compute_services.size(); i++) {
            auto murderer = std::make_shared<NodeKiller>(
                std::default_random_engine(seed + i),
                exponential_distribution,
                restart_overhead,
                "ComputeHost_" + std::to_string(i),
                this->commport,
                "ControllerHost");
            murderer->setSimulation(this->getSimulation());
            murderer->start(murderer, true, false); // Daemonized, no auto-restart
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

        /* Create node on/off turners */
        start_node_killers();

        /* Create the set of idle hosts */
        std::set<std::string> idle_hosts;
        for (const auto &item : _compute_services) {
            idle_hosts.insert(item.first);
        }

        /* Loop until the task completes successfully somewhere */
        while (true) {

            // Submit the task to each idle hosts
            for (const auto &idle_host : idle_hosts) {
                auto job = job_manager->createCompoundJob("");
                job->addSleepAction("", 100);
                WRENCH_INFO("Submitting a job to %s", idle_host.c_str());
                job_manager->submitJob(job, _compute_services.at(idle_host));
            }
            idle_hosts.clear();

            auto event = this->waitForNextEvent();
            if (auto failure_event = std::dynamic_pointer_cast<CompoundJobFailedEvent>(event)) {
                auto hostname = failure_event->compute_service->getHosts().at(0);
                WRENCH_INFO("A job just failed on %s... oh well", hostname.c_str());
            }
            else if (auto success_event = std::dynamic_pointer_cast<CompoundJobCompletedEvent>(event)) {
                auto hostname = success_event->compute_service->getHosts().at(0);
                WRENCH_INFO("A job succeeded on %s... we're done!", hostname.c_str());
                break;
            }
            else if (auto timer_event = std::dynamic_pointer_cast<TimerEvent>(event)) {
                auto hostname = timer_event->message;
                WRENCH_INFO("Host %s just became usable", hostname.c_str());
                idle_hosts.insert(hostname);

            }
        }
        return 0;
    }

    /**
     * @brief Process a compound job completion event
     *
     * @param event: the event
     */
    void Controller::processEventCompoundJobCompletion(const std::shared_ptr<CompoundJobCompletedEvent>& event) {
        /* Retrieve the job that this event is for */
        auto job = event->job;
        /* Print info about all actions in the job */
        WRENCH_INFO("Notified that compound job %s has completed:", job->getName().c_str());
    }
} // namespace wrench
