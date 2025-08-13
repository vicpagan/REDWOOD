#include <chrono>
#include "NodeKiller.h"

#include <wrench/execution_controller/ExecutionControllerMessage.h>

WRENCH_LOG_CATEGORY(node_killer, "Log category for HostSwitcher");


wrench::NodeKiller::NodeKiller(
    const std::default_random_engine rng,
    const std::exponential_distribution<double> exponential_distribution,
    const double restart_overhead,
    const std::string& victim_host,
    S4U_CommPort * notify_commport,
    const std::string& hostname) : Service(hostname, "node_killer"),
                                   _rng(rng),
                                   _exponential_distribution(exponential_distribution),
                                   _restart_overhead(restart_overhead),
                                   _victim_host(victim_host),
                                   _notify_commport(notify_commport) {
}

[[noreturn]] int wrench::NodeKiller::main() {
    TerminalOutput::setThisProcessLoggingColor(TerminalOutput::COLOR_RED);
    WRENCH_INFO("Node killer for %s starting...", _victim_host.c_str());
    while (true) {
        Simulation::sleep(_exponential_distribution(_rng));
        WRENCH_INFO("TURNING OFF HOST %s", _victim_host.c_str());
        Simulation::turnOffHost(_victim_host);
        Simulation::sleep(_restart_overhead);
        WRENCH_INFO("TURNING ON HOST %s", _victim_host.c_str());
        Simulation::turnOnHost(_victim_host);
        WRENCH_INFO("SENDING MESSAGE TO CONTROLER");
        _notify_commport->dputMessage(
            new ExecutionControllerAlarmTimerMessage(_victim_host, 0));
    }
}
