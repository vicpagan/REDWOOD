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
#include "Utils.h"

namespace po = boost::program_options;


unsigned long determine_max_num_compute_nodes(boost::json::object json_data_parallel_input) {
   unsigned long max_size = 0;
    for (const auto& [key, value] : json_data_parallel_input) {
        boost::json::array const& arr = value.as_array();
        max_size = std::max(max_size, arr.size());
    }
    return max_size;
}

double compute_expected_error(std::shared_ptr<wrench::ApplicationSpecs> application_specs,
              boost::json::object json_input,
              boost::json::object json_data_parallel_input,
              unsigned long num_compute_nodes) {

    /* Create the probability computation utility */
    auto probability_computation = std::make_unique<ProbabilityComputation>(application_specs);

    /* Create an execution option comparator function object */
    auto option_comparator = std::make_shared<wrench::ExpectedErrorComparator>(application_specs);

    return 1.0;
}

/**
 * @brief Function to evaluate data parallel options
 */
void evaluate(std::shared_ptr<wrench::ApplicationSpecs> application_specs,
              boost::json::object json_input,
              boost::json::object json_data_parallel_input) {

    auto max_num_compute_nodes = determine_max_num_compute_nodes(json_data_parallel_input);
    for (unsigned long i = 0; i < max_num_compute_nodes; i++) {
        compute_expected_error(application_specs, json_input, json_data_parallel_input, i);
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
    ("json_data_parallel", po::value<std::string>(&json_data_parallel_input_arg)->required()->value_name("<JSON speedup input (str or file path)>"),
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
    } catch (std::exception& e) {
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

    auto application_specs = wrench::ApplicationSpecs::create_application_specs(
        json_input["platform"].as_object(),
        json_input["failures"].as_object(),
        json_input["application"].as_object(),
        json_input["execution"].as_object(),
        json_input["scheduling"].as_object());

    evaluate(application_specs, json_input, json_data_parallel_input);

    return 0;
}
