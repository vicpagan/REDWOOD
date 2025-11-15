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

#include "ProbabilityComputation.h"
#include "scheduling/OptionComparatorFunction.h"
#include "ApplicationSpecs.h"
#include "FunctionGenerator.h"
#include "Utils.h"
#include "scheduling/SchedulingAlgorithmDynamic.h"

namespace po = boost::program_options;


unsigned long determine_max_num_compute_nodes(const boost::json::object& json_data_parallel_input) {
    unsigned long max_size = 0;
    for (const auto& [key, value] : json_data_parallel_input) {
        boost::json::array const& arr = value.as_array();
        max_size = std::max(max_size, arr.size());
    }
    return max_size;
}

double compute_expected_error(boost::json::object json_input,
                              const boost::json::object& json_data_parallel_input,
                              unsigned long num_compute_nodes) {

    // std::cerr << "TWEAKING: NUM_COMPUTE NODES = " << num_compute_nodes << "\n";

    // Compute the acceleration factor for each task
    std::map<std::string, double> speedups;
    for (const auto& [task_name, value] : json_data_parallel_input) {
        boost::json::array const& arr = value.as_array();
        double parallel_speedup = arr[std::min(num_compute_nodes - 1, arr.size() - 1)].as_double();
        speedups[std::string(task_name)] = parallel_speedup;
    }

    // std::cerr << "ORIGINAL " << json_input << "\n\n";

    // Tweak the task descriptions in json_input spec to implement the parallel speedup
    auto& tasks = json_input["application"].get_object()["tasks"].get_array();

    // Find the task by name
    for (auto& task : tasks) {
        std::string task_name(task.at("name").as_string().c_str());
        auto speedup = speedups[task_name];
        auto &task_options = task.get_object()["execution_options"].get_array();

        for (auto& option : task_options) {
            // Get the parameters object (not array!)
            boost::json::object& opt_obj = option.get_object();
            boost::json::object& t_func = opt_obj["t_function"].get_object();
            boost::json::object& t_params = t_func["parameters"].get_object();

            // Modify parameters in place
            for (auto& [key, val] : t_params) {
                // Modify val directly using emplace or by getting the mapped value
                double new_value = val.as_double() / speedup;
                val = new_value;
            }
        }
    }

    // Tweak the lambda value in json_input to scale up the failure rate
    double lambda = json_input["failures"].get_object()["lambda"].as_double();
    json_input["failures"].get_object()["lambda"] = num_compute_nodes * lambda;


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

    double initial_data_size = json_input.at("application").as_object().at("initial_data_size").as_double();
    double initial_error_level = json_input.at("application").as_object().at("initial_error_level").as_double();

    application_specs->prune_decision_tree(0.0);
    application_specs->build_decision_tree(task_functions);
    algorithm->preprocess_decisions(probability_computation.get(),
                                    task_functions,
                                    initial_data_size,
                                    initial_error_level,
                                    application_specs->get_deadline());

    auto optimal_error = algorithm->get_optimal_expected_error();
    std::cerr << "WITH " << num_compute_nodes << " NODES ERROR IS: " << optimal_error << std::endl;
    return optimal_error;
}

/**
 * @brief Function to evaluate data parallel options
 */
void evaluate(const boost::json::object& json_input,
              const boost::json::object& json_data_parallel_input) {
    auto max_num_compute_nodes = determine_max_num_compute_nodes(json_data_parallel_input);
    for (unsigned long i = 1; i <= max_num_compute_nodes; i++) {
        compute_expected_error(json_input, json_data_parallel_input, i);
    }
    return;
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
    double delta_t;
    po::options_description desc("Allowed arguments");
    desc.add_options()
    ("help",
     "Show this help message\n")
    ("json", po::value<std::string>(&json_input_arg)->required()->value_name("<JSON spec input (str or file path)>"),
     "JSON input string or file path\n")
    ("json_data_parallel",
     po::value<std::string>(&json_data_parallel_input_arg)->required()->value_name(
         "<JSON speedup input (str or file path)>"),
     "JSON input string or file path\n")
    ("deadline", po::value<double>(&deadline)->value_name("<deadline>"),
     "Application execution deadline - will override JSON-provided value\n")
    ("lambda", po::value<double>(&lambda)->value_name("<lambda>"),
     "Parameter of the exponential distribution - will override JSON-provided value\n")
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
    boost::json::object json_data_parallel_input;
    if (json_data_parallel_input_arg[0] == '{') {
        json_data_parallel_input = boost::json::parse(json_data_parallel_input_arg).as_object();
    }
    else {
        json_data_parallel_input = readJSONFromFile(json_data_parallel_input_arg);
    }

    if (vm.count("deadline") == 1) {
        json_input.at("execution").as_object().at("deadline") = deadline;
    }

    if (vm.count("lambda") == 1) {
        json_input.at("failures").as_object().at("lambda") = lambda;
    }
    if (vm.count("delta_t") == 1) {
        json_input.at("scheduling").as_object().at("delta_t") = delta_t;
    }


    evaluate(json_input, json_data_parallel_input);

    return 0;
}
