#include <chrono>
#include "NodeKiller.h"

#include <wrench/execution_controller/ExecutionControllerMessage.h>

WRENCH_LOG_CATEGORY(node_killer, "Log category for HostSwitcher");


/**
 * @grief Constructor
 * @param rng: the RNG
 * @param exponential_distribution: the exponential distribution
 * @param restart_overhead: the restart overhead (in seconds)
 * @param victim_host: the hostname of the victim
 * @param notify_commport: the commport on which to send "host is back on" messages
 * @param hostname: the hostname of the host on which this service runs
 */
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

/**
 * @brief The node killer's main method
 * @return never
 */
[[noreturn]] int wrench::NodeKiller::main() {
    TerminalOutput::setThisProcessLoggingColor(TerminalOutput::COLOR_RED);
    WRENCH_INFO("Node killer for %s starting...", _victim_host.c_str());
    while (true) {
        Simulation::sleep(_exponential_distribution(_rng));
        WRENCH_INFO("Turning host %s \"off\"", _victim_host.c_str());
        // Simulation::turnOffHost(_victim_host);
        _notify_commport->dputMessage(new ExecutionControllerAlarmTimerMessage("host_down:" + _victim_host, 0));

        Simulation::sleep(_restart_overhead);
        WRENCH_INFO("Turning host %s \"on\"", _victim_host.c_str());
        // Simulation::turnOnHost(_victim_host);
        _notify_commport->dputMessage(new ExecutionControllerAlarmTimerMessage("host_up:" + _victim_host, 0));
    }
}
