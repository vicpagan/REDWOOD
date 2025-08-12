#include <chrono>
#include "NodeKiller.h"

WRENCH_LOG_CATEGORY(node_killer, "Log category for HostSwitcher");


wrench::NodeKiller::NodeKiller(
    const boost::json::object& failure_spec,
    const std::string& victim_host,
    const std::string& hostname) : Service(hostname, "node_killer") {
    _victim_host = victim_host;
    _lambda = boost::json::value_to<double>(failure_spec.at("lambda"));
    int seed = boost::json::value_to<int>(failure_spec.at("seed"));
    if (seed < 0) {
        std::default_random_engine rng(std::chrono::system_clock::now().time_since_epoch().count());
    }
    else {
        std::default_random_engine rng(static_cast<unsigned int>(seed));
    }
    std::exponential_distribution<double> _exponential_distribution(_lambda);

    _restart_overhead = boost::json::value_to<double>(failure_spec.at("restart_overhead"));
}

[[noreturn]] int wrench::NodeKiller::main() {
    TerminalOutput::setThisProcessLoggingColor(TerminalOutput::COLOR_RED);
    WRENCH_INFO("Node killer for %s starting...", _victim_host.c_str());
    while (true) {
        wrench::Simulation::sleep(_exponential_distribution(_rng));
        wrench::Simulation::turnOffHost(_victim_host);
        wrench::Simulation::sleep(_restart_overhead);
        wrench::Simulation::turnOnHost(_victim_host);
    }
}
