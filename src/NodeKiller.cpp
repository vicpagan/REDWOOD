#include <chrono>
#include "NodeKiller.h"

#include <wrench/execution_controller/ExecutionControllerMessage.h>

WRENCH_LOG_CATEGORY(node_killer, "Log category for HostSwitcher");


namespace wrench {

    std::map<std::string, std::shared_ptr<NodeKiller>> NodeKiller::_node_killers;

    /**
     * @grief Constructor
     * @param rng: the RNG
     * @param exponential_distribution: the exponential distribution
     * @param restart_overhead: the restart overhead (in seconds)
     * @param victim_host: the hostname of the victim
     * @param notify_commport: the commport on which to send "host is back on" messages
     * @param hostname: the hostname of the host on which this service runs
     */
    NodeKiller::NodeKiller(
        const std::default_random_engine rng,
        const std::exponential_distribution<double> exponential_distribution,
        const double restart_overhead,
        const std::string& victim_host,
        S4U_CommPort* notify_commport,
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
    [[noreturn]] int NodeKiller::main() {
        TerminalOutput::setThisProcessLoggingColor(TerminalOutput::COLOR_RED);
        WRENCH_INFO("Node killer for %s starting...", _victim_host.c_str());
        while (true) {
            double sleep_time = _exponential_distribution(_rng);
            std::cout << "Host " << _victim_host << " will run for " << sleep_time << " seconds" << std::endl;
            Simulation::sleep(sleep_time);
            WRENCH_INFO("Turning host %s \"off\"", _victim_host.c_str());
            // Simulation::turnOffHost(_victim_host);
            std::cout << "Host " << _victim_host << " got turned off" << std::endl;
            _notify_commport->dputMessage(new ExecutionControllerAlarmTimerMessage("host_down:" + _victim_host, 0));

            Simulation::sleep(_restart_overhead);
            WRENCH_INFO("Turning host %s \"on\"", _victim_host.c_str());
            // Simulation::turnOnHost(_victim_host);
            std::cout << "Host " << _victim_host << " got turned on" << std::endl;
            _notify_commport->dputMessage(new ExecutionControllerAlarmTimerMessage("host_up:" + _victim_host, 0));
        }
    }

    /**
         * @brief Method to start a node killer on a host
         * @param simulation: the simulation object
         * @param victim: the victim's hostname
         * @param seed: the seed for the RNG
         * @param distribution: the probability distribution
         * @param restart_overhead: the restart overhead
         * @param notify_commport: the commport to notify with events
         * @return A node killer service
         */
    std::shared_ptr<NodeKiller> NodeKiller::start_node_killer(
        Simulation* simulation,
        const std::string& victim,
        const int seed,
        std::exponential_distribution<double> distribution,
        const double restart_overhead,
        S4U_CommPort* notify_commport) {
        // Start the NodeKiller service
        auto murderer = std::make_shared<NodeKiller>(
            std::default_random_engine(seed),
            distribution,
            restart_overhead,
            victim, notify_commport,
            "ControllerHost");
        murderer->setSimulation(simulation);
        murderer->start(murderer, true, false); // Daemonized, no auto-restart
        return murderer;
    }


    /**
     * @brief Start a node killer on each host
     */
    void NodeKiller::start_node_killers(Simulation* simulation,
                                        const std::map<std::string, std::shared_ptr<BareMetalComputeService>>&
                                        compute_services,
                                        int initial_seed,
                                        bool reset_seed,
                                        std::exponential_distribution<double> distribution,
                                        const double restart_overhead,
                                        S4U_CommPort* notify_commport) {
        static int seed = initial_seed;
        if (reset_seed) {
            seed = initial_seed;
        }
        for (auto const& cs : compute_services) {
            auto victim_hostname = cs.second->getHosts().at(0);
            // Kill an existing node killer if any
            if (_node_killers.find(victim_hostname) != _node_killers.end()) {
                _node_killers[victim_hostname]->killActor(); // brutal
            }
            // Start a node killer (note the seed++ there)
            _node_killers[victim_hostname] = NodeKiller::start_node_killer(
                simulation, victim_hostname, seed++, distribution, restart_overhead, notify_commport);
        }
    }
}
