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
#include "Utils.h"

namespace sg4 = simgrid::s4u;
namespace po = boost::program_options;


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
    unsigned long num_repeats;
    double deadline;
    int seed;
    double lambda;
    double e_fail;
    double delta_t;
    po::options_description desc("Allowed arguments");
    desc.add_options()
    ("help",
     "Show this help message\n")
    ("json", po::value<std::string>(&json_input_arg)->required()->value_name("<JSON input (str or file path)>"),
     "JSON input string or file path\n")
    ("num_repeats", po::value<unsigned long>(&num_repeats)->value_name("<number of repeats>"),
     "Number of repeats for each each experiment (i.e., for each algorithm) - will override JSON-provided value\n")
    ("deadline", po::value<double>(&deadline)->value_name("<deadline>"),
         "Application execution deadline - will override JSON-provided value\n")
    ("seed", po::value<int>(&seed)->value_name("<seed>"),
         "RNG seed - will override JSON-provided value\n")
    ("lambda", po::value<double>(&lambda)->value_name("<lambda>"),
         "Parameter of the exponential distribution - will override JSON-provided value\n")
    ("e_fail", po::value<double>(&e_fail)->value_name("<e_fail>"),
         "Error associated to a failed execution - will override JSON-provided value\n")
    ("delta_t", po::value<double>(&delta_t)->value_name("<fixed delta_t>"),
         "delta_t value - will override JSON-provided scheme/value\n");
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
    } catch (std::exception& e) {
        cerr << "Error: " << e.what() << "\n\n";
        std::string usage_string = std::string(argv[0]) + " [--help] --json <JSON input (file)> "
            + "[--log=controller.threshold=info | --wrench-full-log]";
        cerr << "Usage: " << usage_string << "\n";
        exit(1);
    }

    boost::json::object json_input;
    if (json_input_arg[0] == '{') {
        json_input = boost::json::parse(json_input_arg).as_object();
    }
    else {
        json_input = readJSONFromFile(json_input_arg);
    }


    /* Override JSON content if need be */
    if (vm.count("num_repeats") == 1) {
        json_input.at("execution").get_object().at("num_repeats") = num_repeats;
    }
    if (vm.count("deadline") == 1) {
        json_input.at("execution").get_object().at("deadline") = deadline;
    }
    if (vm.count("seed") == 1) {
        json_input.at("failures").get_object().at("seed") = seed;
    }
    if (vm.count("lambda") == 1) {
        json_input.at("failures").get_object().at("lambda") = lambda;
    }
    if (vm.count("e_fail") == 1) {
        json_input.at("execution").get_object().at("e_fail") = e_fail;
    }
    if (vm.count("delta_t") == 1) {
        json_input.at("scheduling").get_object().at("delta_t_scheme").get_object().at("scheme") = "fixed";
        json_input.at("scheduling").get_object().at("delta_t_scheme").get_object().at("parameter") = delta_t;
    }

    /* Instantiating the platform */
    simulation->instantiatePlatform(PlatformCreator(json_input["platform"].as_object()));

    /* Instantiate a bare-metal compute service on each host of the platform */
    auto num_compute_nodes = boost::json::value_to<int>(json_input.at("platform").at("num_compute_nodes"));
    std::vector<std::string> hostnames;
    std::map<std::string, std::shared_ptr<wrench::BareMetalComputeService>> compute_services;
    for (int i = 0; i < num_compute_nodes; i++) {
        std::string hostname = "ComputeHost_" + std::to_string(i);
        hostnames.push_back(hostname);
        auto baremetal_service = simulation->add(new wrench::BareMetalComputeService(
            "ControllerHost", {hostname}, "", {}, {}));
        compute_services[hostname] = baremetal_service;
    }

    auto application_specs = wrench::ApplicationSpecs::create_application_specs(
        json_input["platform"].as_object(),
        json_input["failures"].as_object(),
        json_input["application"].as_object(),
        json_input["execution"].as_object(),
        json_input["scheduling"].as_object(),
        hostnames);

    /* Instantiate the execution controller */
    auto controller = simulation->add(
        new wrench::Controller(
            json_input["application"].as_object(),
            json_input["execution"].as_object(),
            json_input["scheduling"].as_object(),
            application_specs,
            compute_services, "ControllerHost"));

    /* Launch the simulation */
    simulation->launch();

    return 0;
}
