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



namespace sg4 = simgrid::s4u;

/**
 * @brief Functor to instantiate a simulated platform, instead of
 * loading it from an XML file. This function directly uses SimGrid's s4u API
 * (see the SimGrid documentation). This function creates a platform that's
 * identical to that described in the file platform.xml located in the ../data/.
 */
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

    void create_platform() const;
};

