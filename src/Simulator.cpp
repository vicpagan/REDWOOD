/**
 * Copyright (c) 2017-2021. The WRENCH Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <fstream>
#include <iostream>
#include <wrench-dev.h>
#include <boost/json.hpp>

#include "PlatformCreator.h"
#include "Controller.h"
#include "NodeKiller.h"

/**
 * @brief Helper function to read a JSON object from a file
 * @param filepath: the file path
 * @return a boost::json::object object
 */
boost::json::object readJSONFromFile(const std::string& filepath) {
    // Open the file using ifstream
    ifstream file(filepath);

    // Check if the file was opened successfully
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filepath << endl;
        exit(1);
    }

    // Read the whole file into a string
    std::string json_string((istreambuf_iterator<char>(file)),
                            istreambuf_iterator<char>());
    file.close();

    // Parse string into an object
    auto json_object = boost::json::parse(json_string).as_object();
    return json_object;
}


/**
 * @brief Functor to instantiate a simulated platform, instead of
 * loading it from an XML file. This function directly uses SimGrid's s4u API
 * (see the SimGrid documentation). This function creates a platform that's
 * identical to that described in the file platform.xml located in the ../data/.
 */

namespace sg4 = simgrid::s4u;


/**
 * @brief The Simulator's main function
 *
 * @param argc: argument count
 * @param argv: argument array
 * @return 0 on success, non-zero otherwise
 */
int main(int argc, char** argv) {
    /* Create a WRENCH simulation object */
    auto simulation = wrench::Simulation::createSimulation();

    /* Initialize the simulation */
    simulation->init(&argc, argv);

    /* Parse  the command-line arguments */
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] <<
            " --json <JSON input (file)> --wrench-host-shutdown-simulation [--log=controller.threshold=info | --wrench-full-log]" << std::endl;
        exit(1);
    }

    /* Load the json input */
    boost::json::object json_input;
    if (argv[2][0] == '{') {
        json_input = boost::json::parse(argv[2]).as_object();
    }
    else {
        json_input = readJSONFromFile(argv[2]);
    }

    /* Instantiating the platform */
    simulation->instantiatePlatform(PlatformCreator(json_input["platform"].as_object()));

    /* Instantiate a storage service on the ControllerHost */
    auto storage_service = simulation->add(wrench::SimpleStorageService::createSimpleStorageService(
        "ControllerHost", {"/"}, {}, {}));

    /* Instantiate a bare-metal compute service on each host of the platform */
    auto num_compute_nodes = boost::json::value_to<int>(json_input.at("platform").at("num_compute_nodes"));
    std::vector<std::shared_ptr<wrench::BareMetalComputeService>> compute_services;
    compute_services.reserve(num_compute_nodes);
    for (int i = 0; i < num_compute_nodes; i++) {
        auto baremetal_service = simulation->add(new wrench::BareMetalComputeService(
         "ControllerHost", {"ComputeHost_" + std::to_string(i)}, "", {}, {}));
        compute_services.push_back(baremetal_service);
    }


    /* Instantiate an execution controller */
    auto controller = simulation->add(
        new wrench::Controller(
            json_input["failures"].as_object(),
            json_input["application"].as_object(),
            compute_services, storage_service, "ControllerHost"));

    /* Launch the simulation */
    simulation->launch();

    return 0;
}
