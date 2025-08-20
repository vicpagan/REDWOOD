/**
 * Copyright (c) 2017-2021. The WRENCH Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

/**
 ** An execution controller to execute a workflow
 **/

#define GFLOP (1000.0 * 1000.0 * 1000.0)
#define MBYTE (1000.0 * 1000.0)
#define GBYTE (1000.0 * 1000.0 * 1000.0)

// #define COMPUTE_PROBABILITIES 1

#include <iostream>

#include "Controller.h"

#include "FunctionGenerator.h"
#include "NodeKiller.h"
#include "ProbabilityComputation.h"

WRENCH_LOG_CATEGORY(controller, "Log category for Controller");

namespace wrench {
    /**
     * @brief Constructor
     *
     * @param failure_spec: failure specifications
     * @param application_spec: application specifications
     * @param execution_spec: application specifications
     * @param compute_services: a set of compute services available to run actions
     * @param storage_service: the storage service
     * @param hostname: the name of the host on which to start the Execution Controller
     */
    Controller::Controller(const boost::json::object& failure_spec,
                           const boost::json::object& application_spec,
                           const boost::json::object& execution_spec,
                           const std::map<std::string, std::shared_ptr<BareMetalComputeService>>& compute_services,
                           const std::shared_ptr<SimpleStorageService>& storage_service,
                           const std::string& hostname) : ExecutionController(hostname, "controller"),
                                                          _failure_spec(failure_spec),
                                                          _application_spec(application_spec),
                                                          _execution_spec(execution_spec),
                                                          _compute_services(compute_services),
                                                          _storage_service(storage_service) {
        _num_repeats = boost::json::value_to<long>(_execution_spec.at("num_repeats"));
        _deadline = boost::json::value_to<double>(_application_spec.at("deadline"));
        _e_fail = boost::json::value_to<double>(_execution_spec.at("e_fail"));
        _lambda = boost::json::value_to<double>(_failure_spec.at("lambda"));
        _exponential_distribution = std::exponential_distribution<double>(_lambda);
        _seed = boost::json::value_to<int>(_failure_spec.at("seed"));

        if (_seed < 0) {
            _seed = static_cast<int>(std::chrono::system_clock::now().time_since_epoch().count());
        }
    }

    /**
     * @brief Method to start a node killer on a host
     * @param victim: the victim's hostname
     * @param seed: the seed for the RNG
     * @return A node killer service
     */
    std::shared_ptr<NodeKiller> Controller::start_node_killer(const std::string& victim, const int seed) {
        // Turn the host (back) on just in case
        Simulation::turnOnHost(victim);

        // Start the NodeKiller service
        auto restart_overhead = boost::json::value_to<double>(_failure_spec.at("restart_overhead"));
        auto murderer = std::make_shared<NodeKiller>(
            std::default_random_engine(seed),
            _exponential_distribution,
            restart_overhead,
            victim, this->commport,
            "ControllerHost");
        murderer->setSimulation(this->getSimulation());
        murderer->start(murderer, true, false); // Daemonized, no auto-restart
        return murderer;
    }


    /**
     * @brief Start a node killer on each host
     */
    void Controller::start_node_killers() {
        static int seed = _seed;
        for (int i = 0; i < _compute_services.size(); i++) {
            auto victim_hostname = "ComputeHost_" + std::to_string(i);
            // Kill an existing node killer if any
            if (_node_killers.find(victim_hostname) != _node_killers.end()) {
                _node_killers[victim_hostname]->killActor(); // brutal
            }
            // Start a node killer (note the seed++ there)
            _node_killers[victim_hostname] = this->start_node_killer(victim_hostname, seed++);
        }
    }

    /**
     * @brief Calculates expected error recursively for one execution option for a single task
     *
     * @param exec_option_error This is our e(x, y,) for this execution option
     * @param probability_midpoint This is our p_u
     * @param probability_success This is our e^(-lambda * m_j * delta)
     * @param m_j This is m_j in the paper
     * @param n THis is n in the paper
     * @param input_data_size This is our x
     * @param input_error_level This is our y
     * @return The expected error for the selected execution option
     */
    double Controller::calculate_expected_error(double exec_option_error,
                                                double probability_midpoint,
                                                double probability_success,
                                                long m_j,
                                                long n,
                                                double input_data_size,
                                                double input_error_level) {

        if (n < m_j) {
            return _e_fail;
        }

        double reward_success = probability_success * exec_option_error;
        double fail_punishment = 0.0;
        for (long i = 0; i < m_j; i++) {
            fail_punishment += (probability_midpoint * calculate_expected_error(
                exec_option_error, probability_midpoint, probability_success, m_j, n - i - 1, input_data_size, input_error_level));
        }
        return reward_success + fail_punishment;
    }

    /**
     * @brief Selects the best execution option based on the lowest E(x, y, n)
     *
     * @param exec_options Map of execution options for the current task
     * @param input_data_size This is our x
     * @param input_error_level This is our y
     * @param remaining_time This is our n, which is the remaining time until the deadline
     * @return The name of the best execution option
     */
    std::string Controller::select_execution_option(const map<std::string, map<std::string, std::function<double(double, double)>>> & exec_options,
                                                    const double input_data_size,
                                                    const double input_error_level,
                                                    const double remaining_time) {

        double min_error_level = std::numeric_limits<double>::max();
        std::string min_execution_option;

        for (const auto &[option_name, option_functions] : exec_options) {
            const auto exec_option_name = option_name;
            const auto exec_option_time = option_functions.at("t_function")(input_data_size, input_error_level);
            const auto exec_option_error = option_functions.at("e_function")(input_data_size, input_error_level);

            double deltat_computation = _probability_computation->compute_best_deltat(exec_option_time, remaining_time, 1e-3);
            _probability_computation->set_delta_t(deltat_computation);
            double probability_midpoint = _probability_computation->compute_probability_midpoint(exec_option_time, remaining_time);

            // TODO: m_j does not take I/O into account just yet. Need to set up bandwidth.
            auto m_j = static_cast<long>(std::ceil(exec_option_time/deltat_computation));
            auto n = static_cast<long>(std::ceil(remaining_time / deltat_computation));
            auto probability_success = exp(-_lambda * m_j * deltat_computation);

            auto expected_error_option = calculate_expected_error(exec_option_error, probability_midpoint, probability_success, m_j, n, input_data_size, input_error_level);
            if (expected_error_option < min_error_level) {
                min_error_level = expected_error_option;
                min_execution_option = exec_option_name;
            }
        }

        return min_execution_option;
    }


    /**
     * @brief main method of the Controller
     *
     * @return 0 on completion
     *
     */
    int Controller::main() {
        /* Set the logging output to GREEN */
        TerminalOutput::setThisProcessLoggingColor(TerminalOutput::COLOR_GREEN);
        WRENCH_INFO("Controller starting");

        /* Create a job manager so that we can create/submit jobs */
        auto job_manager = this->createJobManager();

        /* Calculate estimate deltat probability to compare to */
        auto restart_overhead = boost::json::value_to<double>(_failure_spec.at("restart_overhead"));
        _probability_computation = std::make_unique<ProbabilityComputation>(_lambda, restart_overhead);

        /* Get initial x and y as well as e_fail from the JSON file */
        auto initial_data_size = boost::json::value_to<double>(_application_spec.at("initial_data_size"));
        auto initial_error_level = boost::json::value_to<double>(_application_spec.at("initial_error_level"));

#ifdef COMPUTE_PROBABILITIES
        double deltat_computation = prob->compute_best_deltat(task_time, _deadline, 1e-3);
        prob->set_delta_t(deltat_computation);
        double probability_upper_bound = prob->compute_probability(task_time, _deadline, false);
        double probability_lower_bound = prob->compute_probability(task_time, _deadline, true);
        double probability_midpoint = (probability_upper_bound + probability_lower_bound) / 2;
#endif

        /* Keep track of number of successes */
        int num_successes = 0;

        /* Collect the functions for each execution option for each task */
        std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>> task_functions;

        auto& tasks = _application_spec.at("tasks").as_array();
        for (const auto& task : tasks) {
            auto task_name = boost::json::value_to<std::string>(task.as_object().at("name"));
            auto& exec_options = task.as_object().at("execution_options").as_array();

            for (const auto& option : exec_options) {
                auto option_name = boost::json::value_to<std::string>(option.as_object().at("name"));

                for (auto function_name : {"t_function", "d_function", "e_function"}) {
                    auto& function = option.as_object().at(function_name).as_object();
                    task_functions[task_name][option_name][function_name] = FunctionGenerator::get_function(function);
                }
            }
        }

        /* Do all the repeats */
        for (int repeat = 0; repeat < _num_repeats; repeat++) {
            /* (Re-)Create node on/off turners */
            start_node_killers();

            /* Create an alarm for the deadline */
            auto alarm = Simulation::getCurrentSimulatedDate() + _deadline;
            WRENCH_INFO("Setting an alarm for repeat %d at time %lf", repeat, alarm);
            this->setTimer(alarm, "time_out:" + std::to_string(repeat));

            /* Create the map of hosts, where entries are either null (if idle) or
             * a submitted job
             */
            std::map<std::string, std::shared_ptr<CompoundJob>> running_jobs;
            for (const auto& item : _compute_services) {
                running_jobs[item.first] = nullptr;;
            }

            auto running_output_data_size = initial_data_size;
            auto running_output_error_level = initial_error_level;

            // TODO: Hard coded in starting task is temporary
            std::string current_task = "task_1";

            /* Loop until the task completes successfully somewhere */
            while (true) {
                // Submit the task to each idle hosts
                for (const auto& [hostname, job] : running_jobs) {
                    if (job == nullptr) {
                        auto new_job = job_manager->createCompoundJob("");
                        std::string selected_exec_option = select_execution_option(task_functions[current_task],
                            running_output_data_size, running_output_error_level,
                            alarm - Simulation::getCurrentSimulatedDate());

                        std::cout << "Selected execution option = " << selected_exec_option << std::endl;
                        new_job->addSleepAction("",
                            task_functions[current_task][selected_exec_option]["t_function"]
                            (running_output_data_size, running_output_error_level));

                        WRENCH_INFO("Submitting a new job to %s", hostname.c_str());
                        job_manager->submitJob(new_job, _compute_services.at(hostname));
                        running_jobs[hostname] = new_job;
                    }
                }

                // Here we could instead call waitForAndProcessNextEvent() and define the handling
                // methods, in case this if-else-if thing becomes too unwieldly
                auto event = this->waitForNextEvent();
                if (auto success_event = std::dynamic_pointer_cast<CompoundJobCompletedEvent>(event)) {
                    auto hostname = success_event->compute_service->getHosts().at(0);
                    std::cout << "REPETITION " << std::to_string(repeat) << " HAS SUCCEEDED (time:" <<
                            Simulation::getCurrentSimulatedDate() << ")" << std::endl;
                    num_successes++;
                    /* TODO: With multiple tasks, we would want to proceed to the next one here, as well as cancel all the rest
                     * Realistically, should this be done with a forced restart of the other hosts?
                     * Or would the other hosts be able to start on the new task and give up the old one instantly?
                     * We also need to update the running input data size and input error level based on the
                     * execution option that was successful
                     */
                    break;
                }
                else if (auto timer_event = std::dynamic_pointer_cast<TimerEvent>(event)) {
                    // This is the catch-all timer-based stuff
                    std::string timeout_prefix = "time_out";
                    std::string hostup_prefix = "host_up";
                    std::string hostdown_prefix = "host_down";

                    // Is it a timeout?
                    if (timer_event->message.compare(0, timeout_prefix.length(), timeout_prefix) == 0) {
                        size_t pos = timer_event->message.find(':');
                        // Check if the colon exists
                        std::string repeat_id = timer_event->message.substr(pos + 1);
                        if (repeat_id != std::to_string(repeat)) {
                            continue; // Spurious timeout
                        }
                        std::cout << "REPETITION " << std::to_string(repeat) << " HAS FAILED (time:" <<
                            Simulation::getCurrentSimulatedDate() << ")" << std::endl;
                        WRENCH_INFO("Deadline reached :(");
                        break;
                    }

                    if (timer_event->message.compare(0, hostup_prefix.length(), hostup_prefix) == 0) {
                        size_t pos = timer_event->message.find(':');
                        std::string hostname = timer_event->message.substr(pos + 1);
                        // Reset the host's entry to nullptr, so that we now know it's idle
                        WRENCH_INFO("Was notified that %s is up again", hostname.c_str());
                        running_jobs[hostname] = nullptr;
                        continue;
                    }

                    if (timer_event->message.compare(0, hostdown_prefix.length(), hostdown_prefix) == 0) {
                        size_t pos = timer_event->message.find(':');
                        std::string hostname = timer_event->message.substr(pos + 1);
                        // Cancel the job
                        WRENCH_INFO("Was notified that %s is down... terminating job", hostname.c_str());
                        job_manager->terminateJob(running_jobs[hostname]);
                        // Leave the job in the map, so that we don't mistake the host as idle
                        continue;
                    }
                }
            }
            // Cancel all pending jobs
            for (const auto& [hostname, job] : running_jobs) {
                if (job) {
                    try {
                        job_manager->terminateJob(job);
                    } catch (ExecutionException& ignore) {
                    }
                }
            }
        }

#ifdef COMPUTE_PROBABILITIES
        double experimental_probability = static_cast<double>(num_successes) / static_cast<double>(_num_repeats);
        double relative_error = std::abs(probability_midpoint - experimental_probability) /
            std::max(std::abs(probability_midpoint), std::abs(experimental_probability));

        std::cout << std::endl << "TOTAL REPEATS = " << _num_repeats << "    TOTAL SUCCESSES = " << num_successes <<
            std::endl;
        std::cout << "EXPERIMENTAL PROBABILITY = " << experimental_probability << std::endl;
        std::cout << "DELTA PROBABILITY = " << probability_midpoint << "    with deltat = " << prob->get_delta_t() <<
            std::endl;
        std::cout << "IS THIS ACCURATE ENOUGH? " << (relative_error < 1e-2 ? "YES" : "NO") << std::endl;
#endif

        return 0;
    }
} // namespace wrench
