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

#include <wrench/util/UnitParser.h>

/**
 * @brief Method to create the simulated platform
 */
void PlatformCreator::create_platform() const {
    // Get the top-level zone
    auto zone = simgrid::s4u::Engine::get_instance()->get_netzone_root();

    auto num_compute_nodes = boost::json::value_to<int>(_platform_spec.at("num_compute_nodes"));

    // Create the UserHost host with no disk
    auto controller_host = zone->add_host("ControllerHost", "10Gf");
    controller_host->set_core_count(1);

    // Create a single network link for now (infinitely fast)
    auto network_link = zone->add_link("network_link", "10000Gbps")->set_latency("0us");

    // Create compute nodes, each with its own disk, which mimics a PFS
    for (int i = 0; i < num_compute_nodes; i++) {
        auto compute_host = zone->add_host("ComputeHost_" + std::to_string(i), "1f");
        compute_host->set_core_count(10);
        auto io_read_bandwidth = wrench::UnitParser::parse_bandwidth(
        boost::json::value_to<std::string>(_platform_spec.at("io_read_bandwidth_per_node")));
        auto io_write_bandwidth = wrench::UnitParser::parse_bandwidth(
            boost::json::value_to<std::string>(_platform_spec.at("io_write_bandwidth_per_node")));

        auto disk = compute_host->add_disk("hard_drive", io_read_bandwidth, io_write_bandwidth);
        disk->set_property("size", "50000GiB");
        disk->set_property("mount", "/");

        // Add route
        sg4::LinkInRoute network_link_in_route{network_link};
        zone->add_route(controller_host,
                        compute_host,
                        {network_link_in_route});
    }
    zone->seal();
}
