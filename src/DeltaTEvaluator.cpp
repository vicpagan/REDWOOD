#include <fstream>
#include <iostream>
#include <chrono>
#include <wrench-dev.h>
#include <boost/json.hpp>
#include <boost/program_options.hpp>

#include "DeltaTEvaluator.h"
#include "ProbabilityComputation.h"
#include "scheduling/OptionComparatorFunction.h"
#include "ApplicationSpecs.h"
#include "FunctionGenerator.h"
#include "Utils.h"
#include "scheduling/SchedulingAlgorithm.h"

namespace po = boost::program_options;

namespace wrench {

    DeltaTEvaluator::DeltaTEvaluator(boost::json::object json_input) : json_input(std::move(json_input)) {}

    std::vector<DeltaTResult> DeltaTEvaluator::evaluate(double min_delta_t, double max_delta_t, double step_size) {
        std::vector<DeltaTResult> results;

        // Setup application specs
        auto application_specs = ApplicationSpecs::create_application_specs(
            json_input.at("platform").as_object(),
            json_input.at("failures").as_object(),
            json_input.at("application").as_object(),
            json_input.at("execution").as_object(),
            json_input.at("scheduling").as_object());

        // Create task functions
        std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>
            task_functions;
        for (const auto& task : json_input.at("application").at("tasks").as_array()) {
            auto task_name = boost::json::value_to<std::string>(task.as_object().at("name"));
            auto& exec_options = task.as_object().at("execution_options").as_array();

            for (const auto& option : exec_options) {
                auto option_name = boost::json::value_to<std::string>(option.as_object().at("name"));

                for (auto function_name : {"t_function", "d_function", "e_function"}) {
                    auto& function = option.as_object().at(function_name).as_object();
                    auto func = FunctionGenerator::get_function(function);
                    task_functions[task_name][option_name][function_name] = func;
                }
            }
        }

        // Create algorithm and probability computation
        auto probability_computation = std::make_unique<ProbabilityComputation>(application_specs);
        auto algorithm = SchedulingAlgorithm::create_scheduling_algorithm("dynamic", application_specs, task_functions, probability_computation.get(), nullptr);

        double initial_data_size = json_input.at("application").as_object().at("initial_data_size").to_number<double>();
        double initial_error_level = json_input.at("application").as_object().at("initial_error_level").to_number<double>();
        double deadline = json_input.at("execution").as_object().at("deadline").to_number<double>();

        application_specs->prune_decision_tree(0.0);
        application_specs->build_decision_tree(task_functions);

        // Evaluate each delta_t value
        for (double delta_t = min_delta_t; delta_t <= max_delta_t; delta_t += step_size) {
            std::cerr << "Evaluating delta_t = " << delta_t << std::endl;

            DeltaTResult result{};
            result.delta_t = delta_t;

            probability_computation->set_delta_t(delta_t);
            algorithm->set_delta_t(delta_t);

            auto start_time = std::chrono::high_resolution_clock::now();

            // Lower bound
            algorithm->reset_preprocessed_decisions();
            algorithm->preprocess_decisions(
                initial_data_size,
                initial_error_level,
                deadline,
                true  // lower_bound
            );
            result.lower_bound_error = algorithm->get_optimal_expected_error();

            // Upper bound
            algorithm->reset_preprocessed_decisions();
            algorithm->preprocess_decisions(
                initial_data_size,
                initial_error_level,
                deadline,
                false  // upper_bound
            );
            result.upper_bound_error = algorithm->get_optimal_expected_error();

            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            result.computation_time_ms = duration.count();

            std::cerr << "  Upper: " << result.upper_bound_error
                      << ", Lower: " << result.lower_bound_error
                      << ", Time: " << result.computation_time_ms << " ms" << std::endl;

            results.push_back(result);
        }

        return results;
    }
} // namespace wrench

int main(int argc, char** argv) {
    std::string json_input_arg;
    double min_delta_t, max_delta_t, step_size;

    po::options_description desc("Allowed arguments");
    desc.add_options()
        ("help", "Show this help message\n")
        ("json", po::value<std::string>(&json_input_arg)->required()->value_name("<JSON spec input>"),
         "JSON input string or file path\n")
        ("min_delta_t", po::value<double>(&min_delta_t)->required()->value_name("<min delta_t>"),
         "Minimum delta_t value\n")
        ("max_delta_t", po::value<double>(&max_delta_t)->required()->value_name("<max delta_t>"),
         "Maximum delta_t value\n")
        ("step_size", po::value<double>(&step_size)->required()->value_name("<step size>"),
         "Step size for delta_t\n");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);

    try {
        if (vm.count("help")) {
            std::cerr << desc;
            exit(0);
        }
        po::notify(vm);
    } catch (std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n\n";
        std::cerr << "Usage: " << argv[0] << " --json <file> --min_delta_t <val> --max_delta_t <val> --step_size <val>\n";
        exit(1);
    }

    boost::json::object json_input;
    if (json_input_arg[0] == '{') {
        json_input = boost::json::parse(json_input_arg).as_object();
    } else {
        json_input = readJSONFromFile(json_input_arg);
    }

    auto evaluator = std::make_unique<wrench::DeltaTEvaluator>(json_input);
    auto results = evaluator->evaluate(min_delta_t, max_delta_t, step_size);

    // Output as JSON
    boost::json::object json_output;
    boost::json::array delta_t_array, upper_array, lower_array, time_array;

    for (const auto& result : results) {
        delta_t_array.push_back(result.delta_t);
        upper_array.push_back(result.upper_bound_error);
        lower_array.push_back(result.lower_bound_error);
        time_array.push_back(result.computation_time_ms);
    }

    json_output["delta_t"] = delta_t_array;
    json_output["upper_bound"] = upper_array;
    json_output["lower_bound"] = lower_array;
    json_output["computation_time_ms"] = time_array;

    std::cout << boost::json::serialize(json_output) << std::endl;

    return 0;
}