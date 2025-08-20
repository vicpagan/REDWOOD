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
#include <boost/program_options.hpp>

#include "PlatformCreator.h"
#include "Controller.h"

namespace sg4 = simgrid::s4u;
namespace po = boost::program_options;


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

    // Define command-line argument options
    std::string json_input_arg;
    po::options_description desc("Allowed arguments");
    desc.add_options()
    ("help",
     "Show this help message\n")
    ("json", po::value<std::string>(&json_input_arg)->required()->value_name("<JSON input (str or file path)>"),
     "JSON input string or file path\n");

    // Parse command-line arguments
    po::variables_map vm;
    po::store(
        po::parse_command_line(argc, argv, desc),
        vm
    );

    try {
        // Print help message and exit if needed
        if (vm.count("help")) {
            std::cerr << desc;
            exit(0);
        }
        // Throw whatever exception in case argument values are erroneous
        po::notify(vm);
    }
    catch (std::exception& e) {
        cerr << "Error: " << e.what() << "\n\n";
        std::string usage_string = std::string(argv[0]) + " [--help] --json <JSON input (file)> "
            + "[--log=controller.threshold=info | --wrench-full-log]";
        cerr << "Usage: " << usage_string << "\n";
        exit(1);
    }

    boost::json::object json_input;
    if (argv[2][0] == '{') {
        json_input = boost::json::parse(json_input_arg).as_object();
    }
    else {
        json_input = readJSONFromFile(json_input_arg);
    }

    /* Instantiating the platform */
    simulation->instantiatePlatform(PlatformCreator(json_input["platform"].as_object()));

    /* Instantiate a storage service on the ControllerHost */
    auto storage_service = simulation->add(wrench::SimpleStorageService::createSimpleStorageService(
        "ControllerHost", {"/"}, {}, {}));

    /* Instantiate a bare-metal compute service on each host of the platform */
    auto num_compute_nodes = boost::json::value_to<int>(json_input.at("platform").at("num_compute_nodes"));
    std::map<std::string, std::shared_ptr<wrench::BareMetalComputeService>> compute_services;
    for (int i = 0; i < num_compute_nodes; i++) {
        std::string hostname = "ComputeHost_" + std::to_string(i);
        auto baremetal_service = simulation->add(new wrench::BareMetalComputeService(
            "ControllerHost", {hostname}, "", {}, {}));
        compute_services[hostname] = baremetal_service;
    }

    /* Instantiate the execution controller */
    auto controller = simulation->add(
        new wrench::Controller(
            json_input["failures"].as_object(),
            json_input["application"].as_object(),
            json_input["execution"].as_object(),
            compute_services, storage_service, "ControllerHost"));

    /* Launch the simulation */
    simulation->launch();

    return 0;
}
