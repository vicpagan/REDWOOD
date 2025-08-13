
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

    /**
     *  @brief An Execution Controller implementation
     */
    class Controller : public ExecutionController {

    public:
        // Constructor
        Controller(
                const boost::json::object& failure_spec,
                const boost::json::object& application_spec,
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

        const boost::json::object _failure_spec;
        const boost::json::object _application_spec;
        const std::map<std::string, std::shared_ptr<BareMetalComputeService>> _compute_services;
        const std::shared_ptr<SimpleStorageService> _storage_service;
    };
}// namespace wrench
#endif//CONTROLLER_H
