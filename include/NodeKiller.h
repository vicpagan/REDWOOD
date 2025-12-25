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
            std::default_random_engine *rng,
            std::exponential_distribution<double> exponential_distribution,
            double restart_overhead,
            const std::string& victim_host,
            S4U_CommPort* notify_commport,
            const std::string& hostname);

        static void start_node_killers(Simulation* simulation,
                                const std::map<std::string, std::shared_ptr<BareMetalComputeService>>&
                                compute_services,
                                int initial_seed,
                                bool reset_seed,
                                std::exponential_distribution<double> distribution,
                                double restart_overhead,
                                S4U_CommPort* notify_commport);

        static void reset_node_killer(Simulation *simulation,
                                    const std::string &victim_host,
                                    std::exponential_distribution<double> distribution,
                                    double restart_overhead,
                                    S4U_CommPort *notify_commport);

        static void reset_all_node_killers(Simulation* simulation,
                                        const std::map<std::string, std::shared_ptr<BareMetalComputeService>>&
                                        compute_services,
                                        std::exponential_distribution<double> distribution,
                                        double restart_overhead,
                                        S4U_CommPort* notify_commport);

    private:
        [[noreturn]] int main() override;
        static std::shared_ptr<NodeKiller> start_node_killer(Simulation* simulation,
                                                             const std::string& victim,
                                                             int seed,
                                                             std::exponential_distribution<double> distribution,
                                                             double restart_overhead,
                                                             S4U_CommPort* notify_commport);

        static std::map<std::string, std::shared_ptr<NodeKiller>> _node_killers;
        static std::map<std::string, std::default_random_engine> _node_killers_generators;

        std::default_random_engine *_rng;
        std::exponential_distribution<double> _exponential_distribution;
        double _restart_overhead;
        std::string _victim_host;
        S4U_CommPort* _notify_commport;

    };
} // namespace wrench
#endif//NODE_KILLER_H
