#ifndef REDWOOD_SYSTEMSPECS_H
#define REDWOOD_SYSTEMSPECS_H

#include <map>
#include <random>
#include <boost/json/object.hpp>


namespace wrench {

    class SystemSpecs {
    public:

        SystemSpecs(const boost::json::object& platform_spec,
            const boost::json::object& failure_spec,
            const boost::json::object& execution_spec);

        static std::shared_ptr<SystemSpecs> create_system_specs(
            const boost::json::object& platform_spec,
            const boost::json::object& failure_spec,
            const boost::json::object& execution_spec);

        int get_num_compute_nodes() const { return _num_compute_nodes; }
        double get_e_fail() const { return _e_fail; }
        double get_io_read_bandwidth() const { return _io_read_bandwidth; }
        double get_io_write_bandwidth() const { return _io_write_bandwidth; }
        double get_restart_overhead() const { return _restart_overhead; }
        double get_deadline() const { return _deadline; }
        double get_lambda() const { return _lambda; }
        std::exponential_distribution<double> get_exponential_distribution() const { return _exponential_distribution; }
        int get_seed() const { return _seed; }

    protected:
        int _num_compute_nodes;
        double _io_read_bandwidth;
        double _io_write_bandwidth;
        double _deadline;
        double _restart_overhead;
        double _e_fail;
        double _lambda;
        std::exponential_distribution<double> _exponential_distribution;
        int _seed;
    };
}

#endif //REDWOOD_SYSTEMSPECS_H