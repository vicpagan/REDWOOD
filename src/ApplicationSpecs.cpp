
#include "ApplicationSpecs.h"
#include <wrench/util/UnitParser.h>

#include <iostream>
#include <boost/json/value_to.hpp>

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

        for (const auto& task : application_spec.at("tasks").as_array()) {
            auto task_name = boost::json::value_to<std::string>(task.as_object().at("name"));
            _task_order.push_back(task_name);
        }
        _num_tasks = static_cast<int>(_task_order.size());

        if (_seed < 0) {
            _seed = static_cast<int>(std::chrono::system_clock::now().time_since_epoch().count());
        }

        _initial_data_size = boost::json::value_to<double>(application_spec.at("initial_data_size"));
        _initial_error_level = boost::json::value_to<double>(application_spec.at("initial_error_level"));

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

    std::string ApplicationSpecs::get_task(const int index) {
        if (index < 0 || index >= _num_tasks) {
            return "";
        }
        return _task_order.at(index);
    }

    void ApplicationSpecs::build_decision_tree(const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options) const {
        _exec_option_decision_tree->build_tree(exec_options);
    }

    void ApplicationSpecs::prune_decision_tree(const double best_error) const {
        _exec_option_decision_tree->prune_tree(best_error);
    }

    bool ApplicationSpecs::decision_tree_empty() const {
        if (_exec_option_decision_tree->root == nullptr || _exec_option_decision_tree->root->num_children == 0) {
            return true;
        }
        return false;
    }

    void ApplicationSpecs::ExecOptionDecisionTree::build_tree(const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options) {
        if (application_specs->_num_tasks == 0) {
            std::cerr << "No tasks!" << std::endl;
            return;
        }

        build_tree_helper(exec_options, 0, application_specs->_exec_option_decision_tree->root, 1.0, 1.0);
    }

    void ApplicationSpecs::ExecOptionDecisionTree::build_tree_helper(const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
        const int task_index,
        const std::shared_ptr<ExecOptionDecisionNode>& parent,
        const double running_data_size_factor,
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

            const double current_data_size_factor = functions.at("d_function")(0, running_data_size_factor);
            child->cumulative_data_size_factor = current_data_size_factor;

            parent->children.push_back(child);
            parent->num_children++;

            build_tree_helper(exec_options, task_index + 1, child, current_data_size_factor, current_error_factor);
        }

    }

    void ApplicationSpecs::ExecOptionDecisionTree::prune_tree(const double best_error) {
        prune_tree_helper(root, best_error);
    }

    bool ApplicationSpecs::ExecOptionDecisionTree::prune_tree_helper(const std::shared_ptr<ExecOptionDecisionNode>& node, const double best_error) {
        if (!node) {
            return true;
        }

        // If this is a leaf node
        if (node->children.empty()) {
            return node->cumulative_error_factor >= best_error;
        }

        // Otherwise, recursively prune children
        auto& children = node->children;
        for (auto child_iterator = children.begin(); child_iterator != children.end(); ) {
            if (prune_tree_helper(*child_iterator, best_error)) {
                child_iterator = children.erase(child_iterator); // remove child
                node->num_children--;
            } else {
                ++child_iterator;
            }
        }

        // If after pruning all children are gone, prune this node as well
        return children.empty();
    }


}
