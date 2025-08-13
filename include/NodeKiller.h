/**
 * Copyright (c) 2017-2018. The WRENCH Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */


#ifndef NODE_KILLER_H
#define NODE_KILLER_H

#include <boost/json.hpp>
#include <wrench-dev.h>

namespace wrench {
    /**
     *  @brief An Execution Controller implementation
     */
    class NodeKiller : public Service {
    public:
        // Constructor
        NodeKiller(
            const std::default_random_engine rng,
            const std::exponential_distribution<double> exponential_distribution,
            const double restart_overhead,
            const std::string& victim_host,
            S4U_CommPort *notify_commport,
            const std::string& hostname);

    private:
        [[noreturn]] int main() override;

        std::default_random_engine _rng;
        std::exponential_distribution<double> _exponential_distribution;
        double _restart_overhead;
        std::string _victim_host;
        S4U_CommPort *_notify_commport;
    };
} // namespace wrench
#endif//NODE_KILLER_H
