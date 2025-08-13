/**
 * Copyright (c) 2017-2021. The WRENCH Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <wrench-dev.h>
#include <boost/json.hpp>

#include "PlatformCreator.h"

/**
 * @brief Functor to instantiate a simulated platform, instead of
 * loading it from an XML file. This function directly uses SimGrid's s4u API
 * (see the SimGrid documentation). This function creates a platform that's
 * identical to that described in the file platform.xml located in the ../data/.
 */


    void PlatformCreator::create_platform() const {
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
        auto network_link = zone->add_link("network_link", "10000Gbps")->set_latency("0us");

        // Create compute nodes
        for (int i = 0; i < boost::json::value_to<int>(_platform_spec.at("num_compute_nodes")); i++) {
            auto compute_host = zone->add_host("ComputeHost_" + std::to_string(i), "1f");
            compute_host->set_core_count(10);
            compute_host->add_disk("hard_drive",
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

