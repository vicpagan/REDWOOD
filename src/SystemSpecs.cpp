#include <wrench/util/UnitParser.h>

#include "SystemSpecs.h"

namespace wrench {

    SystemSpecs::SystemSpecs(const boost::json::object& platform_spec,
                                       const boost::json::object& failure_spec,
                                       const boost::json::object& execution_spec) {

        _num_compute_nodes = boost::json::value_to<int>(platform_spec.at("num_compute_nodes"));
        _io_read_bandwidth = wrench::UnitParser::parse_bandwidth(
        boost::json::value_to<std::string>(platform_spec.at("io_read_bandwidth")));
        _io_write_bandwidth = wrench::UnitParser::parse_bandwidth(
            boost::json::value_to<std::string>(platform_spec.at("io_write_bandwidth")));
        _deadline = boost::json::value_to<double>(execution_spec.at("deadline"));
        _restart_overhead = boost::json::value_to<double>(failure_spec.at("restart_overhead"));
        _e_fail = boost::json::value_to<double>(execution_spec.at("e_fail"));
        _lambda = boost::json::value_to<double>(failure_spec.at("lambda"));
        _exponential_distribution = std::exponential_distribution<double>(_lambda);
        _seed = boost::json::value_to<int>(failure_spec.at("seed"));

        if (_seed < 0) {
            _seed = static_cast<int>(std::chrono::system_clock::now().time_since_epoch().count());
        }
    }

    std::shared_ptr<SystemSpecs> SystemSpecs::create_system_specs(
        const boost::json::object &platform_spec,
        const boost::json::object &failure_spec,
        const boost::json::object &execution_spec) {

        return std::make_shared<SystemSpecs>(platform_spec, failure_spec, execution_spec);
    }

}