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
                           const std::vector<std::shared_ptr<BareMetalComputeService>>& compute_services,
                           const std::shared_ptr<SimpleStorageService>& storage_service,
                           const std::string& hostname) : ExecutionController(hostname, "controller"),
                                                          _failure_spec(_failure_spec),
                                                          _application_spec(application_spec),
                                                          _compute_services(compute_services),
                                                          _storage_service(storage_service) {
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
        for (int i = 0; i < _compute_services.size(); i++) {
            auto murderer = std::shared_ptr<wrench::NodeKiller>(new wrench::NodeKiller(
                _failure_spec,"ComputeHost_" + std::to_string(i), "ControllerHost"));
            murderer->setSimulation(this->getSimulation());
            murderer->start(murderer, true, false); // Daemonized, no auto-restart
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
