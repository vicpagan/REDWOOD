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

#include "Controller.h"
#include <boost/json.hpp>


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

class PlatformCreator {
public:
    explicit PlatformCreator(const boost::json::object& platform_spec) {
        _platform_spec = platform_spec;
    }

    void operator()() const {
        create_platform();
    }

private:
    boost::json::object _platform_spec;

    void create_platform() const {
        // Get the top-level zone
        auto zone = simgrid::s4u::Engine::get_instance()->get_netzone_root();

        // Create the UserHost host with its disk
        auto controller_host = zone->add_host("ControllerHost", "10Gf");
        controller_host->set_core_count(1);
        auto controller_host_disk = controller_host->add_disk("hard_drive",
                                                              boost::json::value_to<std::string>(_platform_spec.at("io_read_bandwidth")),
                                                              boost::json::value_to<std::string>(_platform_spec.at("io_write_bandwidth"))
                                                              );
        controller_host_disk->set_property("size", "50000GiB");
        controller_host_disk->set_property("mount", "/");

        // Create a single network link for now (infinitely fast)
        auto network_link = zone->add_link("network_link", "10000Gps")->set_latency("0us");

        // Create compute nodes
        for (int i = 0; i < boost::json::value_to<int>(_platform_spec.at("num_compute_nodes")); i++) {
            auto compute_host = zone->add_host("ComputeHost_" + std::to_string(i), "1f");
            compute_host->set_core_count(10);
            auto compute_host_disk = compute_host->add_disk("hard_drive",
                                                            "100MBps",
                                                            "100MBps");
            // Add route
            sg4::LinkInRoute network_link_in_route{network_link};
            zone->add_route(controller_host,
                            compute_host,
                            {network_link_in_route});
        }
        zone->seal();
    }
};


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

    /* Parsing of the command-line arguments */
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] <<
            " --json <JSON input (file)> [--log=controller.threshold=info | --wrench-full-log]" << std::endl;
        exit(1);
    }

    /* Load the json input */
    boost::json::object json_input;
    if (argv[1][0] == '{') {
        json_input = boost::json::parse(argv[1]).as_object();
    }
    else {
        json_input = readJSONFromFile(argv[1]);
    }

    /* Instantiating the platform */
    simulation->instantiatePlatform(PlatformCreator(json_input["platform"].as_object()));

    /* Instantiate a storage service on the ControlerHost */
    auto storage_service = simulation->add(wrench::SimpleStorageService::createSimpleStorageService(
        "ControllerHost", {"/"}, {}, {}));

    /* Instantiate a bare-metal compute service on each host of the platform */
    auto baremetal_service = simulation->add(new wrench::BareMetalComputeService(
        "HeadHost", {"ComputeHost"}, "", {}, {}));

    /* Instantiate an execution controller */
    auto controller = simulation->add(
        new wrench::Controller(baremetal_service, storage_service, "UserHost"));

    /* Launch the simulation */
    simulation->launch();

    return 0;
}
