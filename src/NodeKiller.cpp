#include <chrono>
#include <cmath>
#include <limits>

#include "NodeKiller.h"
#include <wrench/execution_controller/ExecutionControllerMessage.h>

WRENCH_LOG_CATEGORY(node_killer, "Log category for HostSwitcher");
namespace wrench {
    std::map<std::string, std::shared_ptr<NodeKiller>> NodeKiller::_node_killers;
    std::map<std::string, std::mt19937> NodeKiller::_node_killers_generators;

    /**
     * @brief Draw a sample from an exponential distribution: one raw 32-bit RNG output is turned into a uniform value u
     *        in [0, 1), and -log(1 - u) / lambda is returned (no standard library distribution is used)
     * @param rng The RNG
     * @param lambda The rate of the distribution
     * @return The sample
     */
    static double portable_exponential_sample(std::mt19937& rng, double lambda) {
        const uint32_t raw = rng();
        double uniform = static_cast<double>(raw) / 4294967296.0;
        if (uniform <= 0.0) {
            uniform = std::numeric_limits<double>::min();
        }
        return -std::log(1.0 - uniform) / lambda;
    }

    /**
     * @brief Constructor
     * @param rng The RNG
     * @param lambda The failure rate parameter
     * @param restart_overhead The restart overhead (in seconds)
     * @param victim_host The hostname of the victim
     * @param notify_commport The commport on which to send "host_down" and "host_up" messages
     * @param hostname The hostname of the host on which this service runs
     */
    NodeKiller::NodeKiller(
        std::mt19937 *rng,
        const double lambda,
        const double restart_overhead,
        const std::string& victim_host,
        S4U_CommPort* notify_commport,
        const std::string& hostname) : Service(hostname, "node_killer"),
                                       _rng(rng),
                                       _lambda(lambda),
                                       _restart_overhead(restart_overhead),
                                       _victim_host(victim_host),
                                       _notify_commport(notify_commport) {
    }

    /**
     * @brief The node killer's main method: forever, wait for an exponentially distributed time (rate lambda), send a
     *        "host_down" message, wait for the restart overhead, and send a "host_up" message
     * @return Never returns
     */
    [[noreturn]] int NodeKiller::main() {
        TerminalOutput::setThisProcessLoggingColor(TerminalOutput::COLOR_RED);
        WRENCH_INFO("Node killer for %s starting...", _victim_host.c_str());
        while (true) {
            double sleep_time = portable_exponential_sample(*_rng, _lambda);
            // std::cout << "Host " << _victim_host << " will run for " << sleep_time << " seconds" << std::endl;
            Simulation::sleep(sleep_time);
            WRENCH_INFO("Turning host %s \"off\"", _victim_host.c_str());
            // Simulation::turnOffHost(_victim_host);
            // std::cout << "Host " << _victim_host << " got turned off" << std::endl;
            _notify_commport->dputMessage(new ExecutionControllerAlarmTimerMessage("host_down:" + _victim_host, 0));
            Simulation::sleep(_restart_overhead);
            WRENCH_INFO("Turning host %s \"on\"", _victim_host.c_str());
            // Simulation::turnOnHost(_victim_host);
            // std::cout << "Host " << _victim_host << " got turned on" << std::endl;
            _notify_commport->dputMessage(new ExecutionControllerAlarmTimerMessage("host_up:" + _victim_host, 0));
        }
    }

    /**
     * @brief Method to start a node killer on a host, using the victim's existing RNG, or creating one if there is none
     * @param simulation The simulation object
     * @param victim The victim's hostname
     * @param seed The seed for a new RNG (-1 when the victim's existing RNG must be reused; throws
     *        std::invalid_argument if there is none)
     * @param lambda The failure rate parameter
     * @param restart_overhead The restart overhead (in seconds)
     * @param notify_commport The commport to notify with events
     * @return A node killer service
     */
    std::shared_ptr<NodeKiller> NodeKiller::start_node_killer(
        Simulation* simulation,
        const std::string& victim,
        const int seed,
        double lambda,
        const double restart_overhead,
        S4U_CommPort* notify_commport) {

        if (_node_killers_generators.find(victim) == _node_killers_generators.end()) {
            if (seed == -1) {
                throw std::invalid_argument("No existing RNG for existing NodeKiller victim '" + victim + "' during reset");
            }
            _node_killers_generators[victim] = std::mt19937(static_cast<std::mt19937::result_type>(seed));
        }

        // start the NodeKiller service
        auto murderer = std::make_shared<NodeKiller>(
            &_node_killers_generators[victim],
            lambda,
            restart_overhead,
            victim, notify_commport,
            "ControllerHost");

        murderer->setSimulation(simulation);
        murderer->start(murderer, true, false); // daemonized, no auto-restart
        return murderer;
    }

    /**
     * @brief Start a node killer on each host, killing any existing one, with a new RNG per host seeded with consecutive
     *        seeds. The seed counter persists across calls (it is a static variable) unless reset_seed is true
     * @param simulation The simulation object
     * @param compute_services The compute services, one per host to inject failures on
     * @param initial_seed The seed for the first host when the counter starts or is reset
     * @param reset_seed Whether to restart the seed counter at initial_seed
     * @param lambda The failure rate parameter
     * @param restart_overhead The restart overhead (in seconds)
     * @param notify_commport The commport to notify with events
     */
    void NodeKiller::start_node_killers(Simulation* simulation,
                                        const std::map<std::string, std::shared_ptr<BareMetalComputeService>>&
                                        compute_services,
                                        int initial_seed,
                                        bool reset_seed,
                                        double lambda,
                                        const double restart_overhead,
                                        S4U_CommPort* notify_commport) {

        static int seed = initial_seed;
        if (reset_seed) {
            seed = initial_seed;
        }

        _node_killers_generators.clear();
        for (auto const& cs : compute_services) {
            auto victim_hostname = cs.second->getHosts().at(0);

            // kill an existing node killer if any
            if (_node_killers.find(victim_hostname) != _node_killers.end()) {
                _node_killers[victim_hostname]->killActor(); // brutal
            }

            // start a node killer (note the seed++ there)
            _node_killers[victim_hostname] = start_node_killer(
                simulation, victim_hostname, seed++, lambda, restart_overhead, notify_commport);
        }
    }

    /**
     * @brief Restart a host's node killer (if it has one), continuing with the same RNG
     * @param simulation The simulation object
     * @param victim_hostname The victim's hostname
     * @param lambda The failure rate parameter
     * @param restart_overhead The restart overhead (in seconds)
     * @param notify_commport The commport to notify with events
     */
    void NodeKiller::reset_node_killer(Simulation *simulation,
        const std::string &victim_hostname,
        double lambda,
        const double restart_overhead,
        S4U_CommPort *notify_commport) {

        if (_node_killers.find(victim_hostname) != _node_killers.end()) {
            _node_killers[victim_hostname]->killActor();
            _node_killers[victim_hostname] = start_node_killer(
                simulation,
                victim_hostname,
                -1,
                lambda,
                restart_overhead,
                notify_commport);
        }
    }

    /**
     * @brief Restart the node killer of each host (see reset_node_killer())
     * @param simulation The simulation object
     * @param compute_services The compute services, one per host
     * @param lambda The failure rate parameter
     * @param restart_overhead The restart overhead (in seconds)
     * @param notify_commport The commport to notify with events
     */
    void NodeKiller::reset_all_node_killers(Simulation* simulation,
        const std::map<std::string, std::shared_ptr<BareMetalComputeService>>&
        compute_services,
        double lambda,
        const double restart_overhead,
        S4U_CommPort* notify_commport) {

        for (auto const& cs : compute_services) {
            auto victim_hostname = cs.second->getHosts().at(0);
            reset_node_killer(simulation,
                victim_hostname,
                lambda,
                restart_overhead,
                notify_commport);
        }
    }

    /**
     * @brief Kill a host's node killer, if it has one
     * @param victim_hostname The victim's hostname
     */
    void NodeKiller::stop_node_killer(const std::string &victim_hostname) {
        if (_node_killers.find(victim_hostname) != _node_killers.end()) {
            _node_killers[victim_hostname]->killActor();
        }
    }
}