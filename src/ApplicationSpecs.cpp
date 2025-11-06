#include <wrench/util/UnitParser.h>

#include "ApplicationSpecs.h"

#include <iostream>

namespace wrench {

    ApplicationSpecs::ApplicationSpecs(const boost::json::object& platform_spec,
                                       const boost::json::object& failure_spec,
                                       const boost::json::object& application_spec,
                                       const boost::json::object& execution_spec,
                                       const boost::json::object& scheduling_spec) {

        _num_compute_nodes = boost::json::value_to<int>(platform_spec.at("num_compute_nodes"));
        _io_read_bandwidth = wrench::UnitParser::parse_bandwidth(
        boost::json::value_to<std::string>(platform_spec.at("io_read_bandwidth")));
        _io_write_bandwidth = wrench::UnitParser::parse_bandwidth(
            boost::json::value_to<std::string>(platform_spec.at("io_write_bandwidth")));
        _deadline = boost::json::value_to<double>(execution_spec.at("deadline"));
        _restart_overhead = boost::json::value_to<double>(failure_spec.at("restart_overhead"));
        _e_fail = boost::json::value_to<double>(execution_spec.at("e_fail"));
        _lambda = boost::json::value_to<double>(failure_spec.at("lambda"));
        _delta_t_parameter = boost::json::value_to<double>(scheduling_spec.at("delta_t_scheme").as_object().at("parameter"));
        _delta_t_scheme = boost::json::value_to<std::string>(scheduling_spec.at("delta_t_scheme").as_object().at("scheme"));
        _exponential_distribution = std::exponential_distribution<double>(_lambda);
        _seed = boost::json::value_to<int>(failure_spec.at("seed"));

        for (int i = 0; i < _num_compute_nodes; i++) {
            std::string hostname = "ComputeHost_" + std::to_string(i);
            _running_hosts.emplace(hostname, std::map<std::string, std::variant<std::string, double>>());
            _running_hosts.at(hostname).emplace("Current Task", "");
            _running_hosts.at(hostname).emplace("Current Exec Option", "");
            _running_hosts.at(hostname).emplace("Current Task Start Time", -1.0);
        }

        for (const auto& task : application_spec.at("tasks").as_array()) {
            auto task_name = boost::json::value_to<std::string>(task.as_object().at("name"));
            _task_order.push_back(task_name);
        }
        _num_tasks = static_cast<int>(_task_order.size());

        if (_seed < 0) {
            _seed = static_cast<int>(std::chrono::system_clock::now().time_since_epoch().count());
        }

        _exec_option_decision_tree = std::make_shared<ExecOptionDecisionTree>(ExecOptionDecisionTree(this, ExecOptionDecisionNode::create_decision_node("", "", false)));
    }

    std::shared_ptr<ApplicationSpecs> ApplicationSpecs::create_application_specs(
        const boost::json::object& platform_spec,
        const boost::json::object& failure_spec,
        const boost::json::object& application_spec,
        const boost::json::object& execution_spec,
        const boost::json::object& scheduling_spec) {

        return std::make_shared<ApplicationSpecs>(platform_spec, failure_spec, application_spec, execution_spec, scheduling_spec);
    }

    void ApplicationSpecs::update_running_host(const std::string& hostname, const std::string& task,
                                               const std::string &exec_option, double start_time) {

        if (_running_hosts.find(hostname) == _running_hosts.end()) {
            throw std::invalid_argument("Hostname '" + hostname + "' not found in running hosts");
        }

        _running_hosts.at(hostname).clear();
        _running_hosts.at(hostname).emplace("Current Task", task);
        _running_hosts.at(hostname).emplace("Current Exec Option", exec_option);
        _running_hosts.at(hostname).emplace("Current Task Start Time", start_time);
    }

    void ApplicationSpecs::reset_running_host(const std::string& hostname) {
        if (_running_hosts.find(hostname) == _running_hosts.end()) {
            throw std::invalid_argument("Hostname '" + hostname + "' not found in running hosts");
        }
        _running_hosts.at(hostname).clear();
        _running_hosts.at(hostname).emplace("Current Task", "");
        _running_hosts.at(hostname).emplace("Current Exec Option", "");
        _running_hosts.at(hostname).emplace("Current Task Start Time", -1.0);
    }

    void ApplicationSpecs::reset_all_running_hosts() {
        for (int i = 0; i < _num_compute_nodes; i++) {
            std::string hostname = "ComputeHost_" + std::to_string(i);
            _running_hosts.at(hostname).clear();
            _running_hosts.at(hostname).emplace("Current Task", "");
            _running_hosts.at(hostname).emplace("Current Exec Option", "");
            _running_hosts.at(hostname).emplace("Current Task Start Time", -1.0);
        }
    }

    std::string ApplicationSpecs::get_task(const int index) {
        if (index < 0 || index >= _num_tasks) {
            return "";
        }
        return _task_order.at(index);
    }

    void ApplicationSpecs::build_decision_tree(const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options) const {
        _exec_option_decision_tree->build_tree(exec_options);
    }

    void ApplicationSpecs::ExecOptionDecisionTree::build_tree(const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options) {
        if (application_specs->_num_tasks == 0) {
            std::cerr << "No tasks!" << std::endl;
            return;
        }

        build_tree_helper(exec_options, 0, application_specs->_exec_option_decision_tree->root, 1.0);
    }

    void ApplicationSpecs::ExecOptionDecisionTree::build_tree_helper(const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
        const int task_index,
        const std::shared_ptr<ExecOptionDecisionNode>& parent,
        const double running_error_factor) {

        if (task_index >= application_specs->_num_tasks) {
            parent->is_leaf = true;
            return;
        }

        const std::string& task_name = application_specs->_task_order[task_index];
        for (const auto& [option_name, functions] : exec_options.at(task_name)) {
            auto child = ExecOptionDecisionNode::create_decision_node(task_name, option_name, false);

            const double current_error_factor = functions.at("e_function")(0, running_error_factor);
            child->cumulative_error_factor = current_error_factor;

            parent->children.push_back(child);
            parent->num_children++;

            build_tree_helper(exec_options, task_index + 1, child, child->cumulative_error_factor);
        }

    }

}
