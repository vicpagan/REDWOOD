
/**
 * Copyright (c) 2017-2018. The WRENCH Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */


#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <wrench-dev.h>
#include <boost/json/object.hpp>

namespace wrench {
    class NodeKiller;

    /**
     *  @brief An Execution Controller implementation
     */
    class Controller : public ExecutionController {

    public:
        // Constructor
        Controller(
                const boost::json::object& failure_spec,
                const boost::json::object& application_spec,
                long num_repeats,
                const std::map<std::string, std::shared_ptr<BareMetalComputeService>> &compute_services,
                const std::shared_ptr<SimpleStorageService> &storage_service,
                const std::string &hostname);

    protected:
        // Overridden method
        void processEventCompoundJobCompletion(const std::shared_ptr<CompoundJobCompletedEvent>&) override;

    private:
        // main() method of the Execution Controller
        int main() override;
        void start_node_killers();
        std::shared_ptr<NodeKiller> start_node_killer(const std::string &victim, int seed);

        std::map<std::string, std::shared_ptr<NodeKiller>> _node_killers;

        const boost::json::object _failure_spec;
        double _deadline;
        const boost::json::object _application_spec;
        const long _num_repeats;
        const std::map<std::string, std::shared_ptr<BareMetalComputeService>> _compute_services;
        const std::shared_ptr<SimpleStorageService> _storage_service;

        double _lambda;
        int _seed;
        std::exponential_distribution<double> _exponential_distribution;
    };
}// namespace wrench
#endif//CONTROLLER_H
