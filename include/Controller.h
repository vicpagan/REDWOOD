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

#include "ProbabilityComputation.h"
#include "ApplicationSpecs.h"
#include "scheduling/SchedulingAlgorithm.h"
#include "scheduling/OptionComparatorFunction.h"

class SchedulingAlgorithm;

namespace wrench {
    class NodeKiller;
    class JobTracker;

    /**
     *  @brief An Execution Controller implementation
     */
    class Controller : public ExecutionController {
    public:
        // Constructor
        Controller(
            const boost::json::object& application_spec,
            const boost::json::object& execution_spec,
            const boost::json::object& scheduling_spec,
            const std::shared_ptr<ApplicationSpecs>& _application_specs,
            const std::map<std::string, std::shared_ptr<BareMetalComputeService>>& compute_services,
            const std::shared_ptr<SimpleStorageService>& storage_service,
            const std::string& hostname);

    protected:

    private:
        int main() override;

        void submit_job(const std::shared_ptr<JobTracker> &job_tracker,
                                                           const std::string& task_name,
                                                           const std::string& execution_option,
                                                           double running_output_data_size,
                                                           double running_output_error_level,
                                                           const std::string& hostname);

        std::shared_ptr<ApplicationSpecs> _application_specs;

        std::unique_ptr<ProbabilityComputation> _probability_computation;

        std::shared_ptr<OptionComparatorFunction> _option_comparator;

        const boost::json::object _application_spec;
        const boost::json::object _execution_spec;
        const boost::json::object _scheduling_spec;
        const std::map<std::string, std::shared_ptr<BareMetalComputeService>> _compute_services;
        const std::shared_ptr<SimpleStorageService> _storage_service;

        std::shared_ptr<JobManager> _job_manager;

        long _num_repeats;

        std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>
        _task_functions;
        std::vector<std::pair<std::vector<std::string>, double>> _execution_combinations;
        std::exponential_distribution<double> _exponential_distribution;
        std::vector<std::shared_ptr<wrench::SchedulingAlgorithm>> _scheduling_algorithms;

        simgrid::s4u::Disk *_storage_disk;

    };
} // namespace wrench
#endif//CONTROLLER_H
