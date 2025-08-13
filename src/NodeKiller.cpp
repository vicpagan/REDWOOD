#include <chrono>
#include "NodeKiller.h"

WRENCH_LOG_CATEGORY(node_killer, "Log category for HostSwitcher");


wrench::NodeKiller::NodeKiller(
    const std::default_random_engine rng,
    const std::exponential_distribution<double> exponential_distribution,
    const double restart_overhead,
    const std::string& victim_host,
    const std::string& hostname) : Service(hostname, "node_killer"),
                                   _rng(rng),
                                   _exponential_distribution(exponential_distribution),
                                   _restart_overhead(restart_overhead) {
    _victim_host = victim_host;
}

[[noreturn]] int wrench::NodeKiller::main() {
    TerminalOutput::setThisProcessLoggingColor(TerminalOutput::COLOR_RED);
    WRENCH_INFO("Node killer for %s starting...", _victim_host.c_str());
    while (true) {
        wrench::Simulation::sleep(_exponential_distribution(_rng));
        std::cerr << wrench::Simulation::getCurrentSimulatedDate() << ": TURNINGT HOST " << _victim_host << " OFF\n";
        wrench::Simulation::turnOffHost(_victim_host);
        wrench::Simulation::sleep(_restart_overhead);
        std::cerr << "TURNING HOST " << _victim_host << " BACK ON\n";
        wrench::Simulation::turnOnHost(_victim_host);
    }
}
