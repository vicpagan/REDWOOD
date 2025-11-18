#include "FunctionGenerator.h"

#include <iostream>

std::map<std::string, std::function<double(double, double)>> FunctionGenerator::_function_registry;

std::function<double(double, double)> FunctionGenerator::get_function(const boost::json::object& spec) {
    std::string key = boost::json::serialize(spec);
    // Lookup the registry in case we've done that one before
    if (_function_registry.find(key) == _function_registry.end()) {
        auto function_type = boost::json::value_to<std::string>(spec.at("type"));
        if (function_type == "affine") {
            _function_registry[key] = FunctionGenerator::AffineFunctor(
                spec.at("parameters").as_object().at("a").to_number<double>(),
                spec.at("parameters").as_object().at("b").to_number<double>(),
                spec.at("parameters").as_object().at("c").to_number<double>());
        } else if (function_type == "quadratic") {
            _function_registry[key] = FunctionGenerator::QuadraticFunctor(
                spec.at("parameters").as_object().at("a").to_number<double>(),
                spec.at("parameters").as_object().at("b").to_number<double>(),
                spec.at("parameters").as_object().at("c").to_number<double>(),
                spec.at("parameters").as_object().at("d").to_number<double>(),
                spec.at("parameters").as_object().at("e").to_number<double>());
        } else {
            throw std::invalid_argument("Unknown function type '" + function_type + "'");
        }
    }
    // Return the function (which is really a functor)
    return _function_registry[key];
}


