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
#include <boost/program_options.hpp>
#include <utility>

#include "ProbabilityComputation.h"
#include "scheduling/OptionComparatorFunction.h"
#include "ApplicationSpecs.h"
#include "FunctionGenerator.h"
#include "Utils.h"
#include "scheduling/SchedulingAlgorithmDynamic.h"
#include "DataParallelEvaluator.h"

namespace po = boost::program_options;

wrench::DataParallelEvaluator::DataParallelEvaluator(boost::json::object json_input,
                                                     unsigned long max_num_compute_nodes) :
    json_input(std::move(json_input)), max_num_compute_nodes(max_num_compute_nodes) {
}

double wrench::DataParallelEvaluator::compute_expected_error(unsigned long num_compute_nodes) {
    // std::cerr << "TWEAKING: NUM_COMPUTE NODES = " << num_compute_nodes << "\n";

    // Compute the acceleration factor for each task
    std::map<std::string, std::vector<double>> speedups;
    for (const auto& task : json_input["application"].get_object()["tasks"].as_array()) {
        auto task_name = std::string(task.as_object().at("name").as_string().c_str());
        speedups[task_name] = {};
        auto& task_options = task.as_object().at("execution_options").as_array();
        for (auto& option : task_options) {
            auto opt_obj = option.as_object();
            double parallel_speedup;
            if (opt_obj.contains("speedup_measurements") && opt_obj.contains("parallel_efficiency")) {
                throw std::invalid_argument("Speedup spec for an option of task " + task_name + " contains both a 'speedup_measurements' and a 'parallel efficiency' key, which is not allowed");
            }
            if (opt_obj.contains("speedup_measurements")) {
                auto speedup_array = opt_obj["speedup_measurements"].as_array();
                parallel_speedup = speedup_array[std::min(num_compute_nodes - 1, speedup_array.size() - 1)].
                    to_number<double>();
            } else if (opt_obj.contains("parallel_efficiency")) {
                auto parallel_efficiency = opt_obj["parallel_efficiency"].to_number<double>();
                parallel_speedup = static_cast<double>(num_compute_nodes) * parallel_efficiency;
            } else {
                throw std::invalid_argument("Speedup spec invalid/missing for an option of task " + task_name);
            }
            speedups[task_name].push_back(parallel_speedup);
        }
    }

    // Tweak the task descriptions in json_input spec to implement the parallel speedup
    auto& tasks = json_input["application"].get_object()["tasks"].get_array();

    // Find the task by name
    for (auto& task : tasks) {
        std::string task_name(task.at("name").as_string().c_str());
        auto& task_options = task.get_object()["execution_options"].get_array();

        for (int i=0; i<task_options.size(); i++) {
            auto option = task_options[i];
            // Get the parameters object (not array!)
            boost::json::object& opt_obj = option.get_object();
            boost::json::object& t_func = opt_obj["t_function"].get_object();
            boost::json::object& t_params = t_func["parameters"].get_object();

            // Modify parameters in place
            for (auto& [key, val] : t_params) {
                // Modify val directly using emplace or by getting the mapped value
                double new_value = val.to_number<double>() / speedups[task_name].at(i);
                val = new_value;
            }
        }
    }

    // Tweak the lambda value in json_input to scale up the failure rate
    double lambda = json_input["failures"].get_object()["lambda"].to_number<double>();
    json_input["failures"].get_object()["lambda"] = static_cast<double>(num_compute_nodes) * lambda;


    // std::cerr << "TWEAKED " << json_input << "\n";

    auto application_specs = wrench::ApplicationSpecs::create_application_specs(
        json_input.at("platform").as_object(),
        json_input.at("failures").as_object(),
        json_input.at("application").as_object(),
        json_input.at("execution").as_object(),
        json_input.at("scheduling").as_object());

    // Create the task functions
    std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>
        task_functions;
    for (const auto& task : json_input.at("application").at("tasks").as_array()) {
        std::vector<std::pair<std::string, std::function<double(double, double)>>> options_for_task;

        auto task_name = boost::json::value_to<std::string>(task.as_object().at("name"));
        auto& exec_options = task.as_object().at("execution_options").as_array();

        for (const auto& option : exec_options) {
            auto option_name = boost::json::value_to<std::string>(option.as_object().at("name"));

            for (auto function_name : {"t_function", "d_function", "e_function"}) {
                auto& function = option.as_object().at(function_name).as_object();
                auto func = FunctionGenerator::get_function(function);

                if (static_cast<std::string>(function_name) == "e_function") {
                    options_for_task.emplace_back(option_name, func);
                }
                task_functions[task_name][option_name][function_name] = func;
            }
        }
    }

    // Create an unused scheduling algorithm
    auto algorithm = new wrench::SchedulingAlgorithmDynamic(application_specs);

    // Create a probability computation object
    auto probability_computation = std::make_unique<ProbabilityComputation>(application_specs);

    double initial_data_size = json_input.at("application").as_object().at("initial_data_size").to_number<double>();
    double initial_error_level = json_input.at("application").as_object().at("initial_error_level").to_number<double>();

    application_specs->prune_decision_tree(0.0);
    application_specs->build_decision_tree(task_functions);
    algorithm->preprocess_decisions(probability_computation.get(),
                                    task_functions,
                                    initial_data_size,
                                    initial_error_level,
                                    application_specs->get_deadline());

    auto optimal_error = algorithm->get_optimal_expected_error();
    return optimal_error;
}

/**
 * @brief Function to evaluate data parallel options
 */
std::vector<double> wrench::DataParallelEvaluator::evaluate() {
    std::vector<double> errors;
    errors.reserve(this->max_num_compute_nodes);
    for (unsigned long num_compute_nodes = 1; num_compute_nodes <= this->max_num_compute_nodes; num_compute_nodes++) {
        errors.push_back(compute_expected_error(num_compute_nodes));
    }
    return errors;
}


/**
 * @brief The Simulator's main function
 *
 * @param argc: argument count
 * @param argv: argument array
 * @return 0 on success, non-zero otherwise
 */
int main(int argc, char** argv) {
    // Define command-line argument options
    std::string json_input_arg;
    std::string json_data_parallel_input_arg;
    unsigned long num_repeats;
    double deadline;
    int seed;
    double lambda;
    double e_fail;
    double delta_t;
    po::options_description desc("Allowed arguments");
    desc.add_options()
    ("help",
     "Show this help message\n")
    ("json", po::value<std::string>(&json_input_arg)->required()->value_name("<JSON spec input (str or file path)>"),
     "JSON input string or file path\n")
    ("deadline", po::value<double>(&deadline)->value_name("<deadline>"),
     "Application execution deadline - will override JSON-provided value\n")
    ("lambda", po::value<double>(&lambda)->value_name("<lambda>"),
     "Parameter of the exponential distribution - will override JSON-provided value\n")
    ("e_fail", po::value<double>(&e_fail)->value_name("<e_fail>"),
     "Error associated to a failed execution - will override JSON-provided value\n")
    ("delta_t", po::value<double>(&delta_t)->value_name("<delta_t>"),
     "delta_t value - will override JSON-provided value\n");
    // Parse command-line arguments
    po::variables_map vm;
    po::store(
        po::parse_command_line(argc, argv, desc),
        vm
    );

    try {
        // Print help message and exit if needed
        if (vm.count("help")) {
            std::cerr << desc;
            exit(0);
        }
        // Throw whatever exception in case argument values are erroneous
        po::notify(vm);
    }
    catch (std::exception& e) {
        cerr << "Error: " << e.what() << "\n\n";
        std::string usage_string = std::string(argv[0]) + " [--help] --json <JSON spec input (file)> "
            + "--json_data_parallel <JSON speedup input (str or file path)> "
            + "[--log=controller.threshold=info | --wrench-full-log]";
        cerr << "Usage: " << usage_string << "\n";
        exit(1);
    }

    boost::json::object json_input;
    if (json_input_arg[0] == '{') {
        json_input = boost::json::parse(json_input_arg).as_object();
    }
    else {
        json_input = readJSONFromFile(json_input_arg);
    }

    if (vm.count("deadline") == 1) {
        json_input.at("execution").as_object().at("deadline") = deadline;
    }
    if (vm.count("lambda") == 1) {
        json_input.at("failures").as_object().at("lambda") = lambda;
    }
    if (vm.count("e_fail") == 1) {
        json_input.at("execution").as_object().at("e_fail") = e_fail;
    }
    if (vm.count("delta_t") == 1) {
        json_input.at("scheduling").as_object().at("delta_t") = delta_t;
    }

    auto evaluator = std::make_unique<wrench::DataParallelEvaluator>(json_input, 20);
    auto results = evaluator->evaluate();
    for (int i = 0; i < results.size(); ++i) {
        std::cerr << (i + 1) << " nodes:  e = " << results.at(i) << std::endl;
    }

    return 0;
}
