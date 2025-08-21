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

class SchedulingAlgorithm;

namespace wrench {
    class NodeKiller;

    /**
     *  @brief An Execution Controller implementation
     */
    class Controller : public ExecutionController {
    public:
        // Constructor
        Controller(
            const boost::json::object& platform_spec,
            const boost::json::object& failure_spec,
            const boost::json::object& application_spec,
            const boost::json::object& execution_spec,
            const boost::json::object& scheduling_spec,
            const std::map<std::string, std::shared_ptr<BareMetalComputeService>>& compute_services,
            const std::shared_ptr<SimpleStorageService>& storage_service,
            const std::string& hostname);

    protected:

    private:
        int main() override;

        std::shared_ptr<CompoundJob> create_and_submit_job(const std::string& task_name,
                                                           const std::string& execution_option,
                                                           double running_output_data_size,
                                                           double running_output_error_level,
                                                           const std::string& hostname);

        // void start_probability_computation(double lambda, double restart_overhead);

        // std::string select_execution_option(const map<std::string, map<std::string, std::function<double(double, double)>>> &exec_options,
        //     const double input_data_size, const double input_error_level, const double remaining_time);
        // double calculate_expected_error(double exec_option_error,
        //                                 double probability_midpoint,
        //                                 double probability_success,
        //                                 long m_j,
        //                                 long n,
        //                                 double input_data_size,
        //                                 double input_error_level);

        std::unique_ptr<ProbabilityComputation> _probability_computation;

        const boost::json::object _platform_spec;
        const boost::json::object _failure_spec;
        const boost::json::object _application_spec;
        const boost::json::object _execution_spec;
        const boost::json::object _scheduling_spec;
        const std::map<std::string, std::shared_ptr<BareMetalComputeService>> _compute_services;
        const std::shared_ptr<SimpleStorageService> _storage_service;

        std::shared_ptr<JobManager> _job_manager;

        double _deadline;
        double _restart_overhead;
        double _e_fail;
        long _num_repeats;
        double _lambda;
        double _delta_t;
        double _delta_t_precision;
        int _seed;
        double _io_read_bandwidth;
        double _io_write_bandwidth;
        std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>
        _task_functions;
        std::exponential_distribution<double> _exponential_distribution;
        std::vector<std::shared_ptr<SchedulingAlgorithm>> _scheduling_algorithms;

        simgrid::s4u::Disk *_storage_disk;

    };
} // namespace wrench
#endif//CONTROLLER_H
