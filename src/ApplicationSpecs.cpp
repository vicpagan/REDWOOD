
#include "ApplicationSpecs.h"
#include <wrench/util/UnitParser.h>

#include <iostream>
#include <boost/json/value_to.hpp>
#include <stack>

#include "FunctionGenerator.h"

namespace wrench {

    ApplicationSpecs::ApplicationSpecs(const boost::json::object& platform_spec,
                                       const boost::json::object& failure_spec,
                                       const boost::json::object& application_spec,
                                       const boost::json::object& execution_spec,
                                       const boost::json::object& scheduling_spec,
                                       const std::vector<std::string>& hostnames) : _hostnames(hostnames) {

        _num_compute_nodes = boost::json::value_to<int>(platform_spec.at("num_compute_nodes"));

        _io_read_bandwidth_per_node = wrench::UnitParser::parse_bandwidth(
        boost::json::value_to<std::string>(platform_spec.at("io_read_bandwidth_per_node")));
        _io_write_bandwidth_per_node = wrench::UnitParser::parse_bandwidth(
            boost::json::value_to<std::string>(platform_spec.at("io_write_bandwidth_per_node")));

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

            auto in_situ_with_next_task = boost::json::value_to<bool>(task.as_object().at("in_situ_with_next_task"));
            _in_situ_tasks.emplace(task_name, in_situ_with_next_task);

            std::vector<std::pair<std::string, std::function<double(double, double)>>> options_for_task;
            auto& exec_options = task.as_object().at("execution_options").as_array();
            for (const auto& option : exec_options) {
                auto option_name = boost::json::value_to<std::string>(option.as_object().at("name"));

                for (auto function_name : {"t_function", "d_function", "e_function"}) {
                    auto& function = option.as_object().at(function_name).as_object();
                    auto func = FunctionGenerator::get_function(function);

                    if (static_cast<std::string>(function_name) == "e_function") {
                        options_for_task.emplace_back(option_name, func);
                    }
                    _task_functions[task_name][option_name][function_name] = func;
                }
            }
        }
        _num_tasks = static_cast<int>(_task_order.size());

        merge_in_situ_tasks(_task_functions);

        if (_seed < 0) {
            _seed = static_cast<int>(std::chrono::system_clock::now().time_since_epoch().count());
        }

        _initial_data_size = boost::json::value_to<double>(application_spec.at("initial_data_size"));
        _initial_error_level = boost::json::value_to<double>(application_spec.at("initial_error_level"));

        for (const auto& hostname : _hostnames) {
            _hosts_decision_trees[hostname] = std::make_shared<ExecOptionDecisionTree>(ExecOptionDecisionTree(this, ExecOptionDecisionNode::create_decision_node("", "", false)));
            _hosts_current_scheduled_tasks[hostname] = _task_order.at(0);
            _hosts_running_data_size_and_error_level[hostname] = {_initial_data_size, _initial_error_level};
        }
    }

    std::shared_ptr<ApplicationSpecs> ApplicationSpecs::create_application_specs(
        const boost::json::object& platform_spec,
        const boost::json::object& failure_spec,
        const boost::json::object& application_spec,
        const boost::json::object& execution_spec,
        const boost::json::object& scheduling_spec,
        const std::vector<std::string>& hostnames) {

        return std::make_shared<ApplicationSpecs>(platform_spec, failure_spec, application_spec, execution_spec, scheduling_spec, hostnames);
    }

    void ApplicationSpecs::merge_in_situ_tasks(std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& task_functions) {
        int i = 0;
        while (i < _num_tasks) {
            std::string task_name = get_task(i);
            if (!_in_situ_tasks.at(task_name)) {
                i++;
                continue;
            }

            // Collect the tasks to merge
            int num_tasks_to_merge = 1;

            std::vector<std::string> tasks_to_merge;
            tasks_to_merge.push_back(get_task(i));

            while (_in_situ_tasks.at(tasks_to_merge.back()) && i + tasks_to_merge.size() < _num_tasks) {
                tasks_to_merge.push_back(get_task(i + static_cast<int>(tasks_to_merge.size())));
                num_tasks_to_merge++;
            }

            // Need to cover edge case of last task having the in_situ_with_next_task flag set to true (for some stupid reason just in case I guess)
            if (num_tasks_to_merge == 1) {
                i++;
                continue;
            }

            // Merge the list of tasks into a single task
            std::string merged_tasks_name = tasks_to_merge[0];
            auto merged_tasks = task_functions.at(tasks_to_merge[0]); // copy first task's options

            for (size_t j = 1; j < tasks_to_merge.size(); j++) {
                merged_tasks_name += "+" + tasks_to_merge[j];
                std::map<std::string, std::map<std::string, std::function<double(double, double)>>> next_task_to_combine;

                for (const auto& [opt1_name, opt1_functions] : merged_tasks) {
                    auto t1 = opt1_functions.at("t_function");
                    auto d1 = opt1_functions.at("d_function");
                    auto e1 = opt1_functions.at("e_function");

                    for (const auto& [opt2_name, opt2_functions] : task_functions.at(tasks_to_merge[j])) {
                        auto t2 = opt2_functions.at("t_function");
                        auto d2 = opt2_functions.at("d_function");
                        auto e2 = opt2_functions.at("e_function");

                        std::string merged_opt_name = opt1_name;
                        merged_opt_name += "+";
                        merged_opt_name += opt2_name;

                        next_task_to_combine[merged_opt_name]["t_function"] =
                            [t1, d1, e1, t2](const double x, const double e) {
                                return t1(x, e) + t2(d1(x, e), e1(x, e));
                            };
                        next_task_to_combine[merged_opt_name]["d_function"] =
                            [d1, e1, d2](const double x, const double e) {
                                return d2(d1(x, e), e1(x, e));
                            };
                        next_task_to_combine[merged_opt_name]["e_function"] =
                            [d1, e1, e2](const double x, const double e) {
                                return e2(d1(x, e), e1(x, e));
                            };
                    }
                }
                merged_tasks = std::move(next_task_to_combine);
            }

            // Update _task_functions with merged tasks and update application_specs
            task_functions[merged_tasks_name] = std::move(merged_tasks);
            for (const auto& task : tasks_to_merge) {
                task_functions.erase(task);
            }
            _task_order.erase(_task_order.begin() + i, _task_order.begin() + i + num_tasks_to_merge);
            _task_order.insert(_task_order.begin() + i, merged_tasks_name);
            _num_tasks = static_cast<int>(_task_order.size());

            i++;
        }
    }

    void ApplicationSpecs::update_host_running_data_size(const std::string& hostname, const double data_size) {
        _hosts_running_data_size_and_error_level[hostname].first = data_size;
    }

    void ApplicationSpecs::update_host_running_error_level(const std::string& hostname, const double error_level) {
        _hosts_running_data_size_and_error_level[hostname].second = error_level;
    }

    double ApplicationSpecs::get_host_running_data_size(const std::string& hostname) const {
        return _hosts_running_data_size_and_error_level.at(hostname).first;
    }

    double ApplicationSpecs::get_host_running_error_level(const std::string& hostname) const {
        return _hosts_running_data_size_and_error_level.at(hostname).second;
    }

    void ApplicationSpecs::update_host_task_to_schedule(const std::string& hostname, const int task_index) {
        _hosts_current_scheduled_tasks[hostname] = _task_order.at(task_index);
    }

    void ApplicationSpecs::update_host_task_to_schedule(const std::string& hostname, const std::string& task_name) {
        _hosts_current_scheduled_tasks[hostname] = task_name;
    }

    void ApplicationSpecs::increment_host_task_to_schedule(const std::string& hostname) {
        std::string current_task = _hosts_current_scheduled_tasks[hostname];
        size_t i = 0;
        while (_task_order.at(i) != current_task) {
            i++;
        }
        i++;

        if (i < _num_tasks) {
            _hosts_current_scheduled_tasks[hostname] = _task_order.at(i);
        } else {
            _hosts_current_scheduled_tasks[hostname] = "";
        }
    }

    std::string ApplicationSpecs::get_host_task_to_schedule(const std::string& hostname) const {
        std::cerr << "Task to schedule for host " << hostname << " is " << _hosts_current_scheduled_tasks.at(hostname) << "\n";
        return _hosts_current_scheduled_tasks.at(hostname);
    }

    int ApplicationSpecs::get_host_task_to_schedule_index(const std::string& hostname) const {
        std::string current_task = _hosts_current_scheduled_tasks.at(hostname);
        std::cerr << "Host " << hostname << " current task: " << current_task << std::endl;
        return get_task_index(_hosts_current_scheduled_tasks.at(hostname));
    }

    std::string ApplicationSpecs::get_task(const int index) const {
        if (index < 0 || index >= _num_tasks) {
            return "";
        }
        return _task_order.at(index);
    }

    int ApplicationSpecs::get_task_index(const std::string& task_name) const {
        std::cerr << "Getting task index for task " << task_name << std::endl;
        std::string current_task = _task_order.at(0);
        int i = 0;
        while (current_task != task_name && i < _num_tasks) {
            i++;
            current_task = _task_order.at(i);
        }
        if (i == _num_tasks) {
            return -1;
        }
        return i;
    }

    const ApplicationSpecs::ExecOptionDecisionNode* ApplicationSpecs::get_host_current_decision_node(const std::string& hostname) const {
        return _hosts_current_decision_nodes.at(hostname).get();
    }

    void ApplicationSpecs::increment_host_current_decision_node(const std::string& hostname, const std::string &task_name, const std::string &execution_option) {
        auto decision_node = _hosts_current_decision_nodes.at(hostname);
        if (!decision_node->is_leaf) {
            for (const auto& child : decision_node->children) {
                if (child->task == task_name && child->execution_option == execution_option) {
                    decision_node = child;
                    break;
                }
            }
            _hosts_current_decision_nodes[hostname] = decision_node;
            _hosts_decision_history[hostname].push_back(execution_option);
        } else {
            std::cerr << "Host has finished its execution" << std::endl;
        }
    }

    void ApplicationSpecs::update_host_current_decision_node(const std::string& hostname, const std::string& reference_hostname) {
        std::cerr << "updating " << hostname << " current decision node with reference hostname " << reference_hostname << std::endl;
        auto current_decision_node = _hosts_decision_trees.at(hostname)->root;
        for (const auto& entry : _hosts_decision_history[reference_hostname]) {
            std::cerr << "current entry: " << entry << std::endl;
            bool found = false;
            for (const auto& child : current_decision_node->children) {
                if (child->execution_option == entry) {
                    current_decision_node = child;
                    found = true;
                    std::cerr << "FOUND!" << std::endl;
                    break;
                }
            }

            if (!found) {
                std::string available;
                for (const auto& child : current_decision_node->children) {
                    available += child->execution_option + " ";
                }

                throw std::runtime_error("No matching execution_option='" + entry + "' at node. Available: " + available);
            }
        }
        _hosts_current_decision_nodes[hostname] = current_decision_node;
        std::cerr << "node ptr: " << current_decision_node << "\n";
        _hosts_decision_history[hostname] = _hosts_decision_history.at(reference_hostname);
    }

    bool ApplicationSpecs::can_possibly_do_better(const std::string& hostname, const std::string& reference_hostname) const {
        double min_error_factor = std::numeric_limits<double>::infinity();
        double max_error_factor = -std::numeric_limits<double>::infinity();
        const auto current_host_decision_node = _hosts_current_decision_nodes.at(hostname);
        const auto reference_host_decision_node = _hosts_current_decision_nodes.at(reference_hostname);

        std::stack<std::shared_ptr<ExecOptionDecisionNode>> stack;
        stack.push(current_host_decision_node);
        while (!stack.empty()) {
            const auto current_decision_node = stack.top();
            stack.pop();

            if (current_decision_node->is_leaf) {
                min_error_factor = std::min(min_error_factor, current_decision_node->cumulative_error_factor);
            } else {
                for (const auto& child : current_decision_node->children) {
                    stack.push(child);
                }
            }
        }

        stack.push(reference_host_decision_node);
        while (!stack.empty()) {
            const auto current_decision_node = stack.top();
            stack.pop();

            if (current_decision_node->is_leaf) {
                max_error_factor = std::max(max_error_factor, current_decision_node->cumulative_error_factor);
            } else {
                for (const auto& child : current_decision_node->children) {
                    stack.push(child);
                }
            }
        }

        return min_error_factor < max_error_factor;
    }

    void ApplicationSpecs::reset_host_current_decision_node(const std::string& hostname) {
        _hosts_current_decision_nodes[hostname] = _hosts_decision_trees.at(hostname)->root;
    }

    void ApplicationSpecs::reset_all_hosts_current_decision_nodes() {
        for (const auto& hostname : _hostnames) {
            _hosts_current_decision_nodes[hostname] = _hosts_decision_trees.at(hostname)->root;
        }
    }

    void ApplicationSpecs::update_host_decision_history(const std::string& hostname, const std::string& reference_hostname) {
        _hosts_decision_history[hostname] = _hosts_decision_history.at(reference_hostname);
    }

    void ApplicationSpecs::reset_host_decision_history(const std::string& hostname) {
        _hosts_decision_history[hostname].clear();
    }

    void ApplicationSpecs::reset_all_hosts_decision_history() {
        for (const auto& hostname : _hostnames) {
            _hosts_decision_history[hostname].clear();
        }
    }

    ////////////////// DECISION TREE MANAGEMENT METHODS //////////////////
    
    void ApplicationSpecs::build_decision_trees() const {
        for (const auto& hostname : _hostnames) {
            this->build_decision_tree(hostname);
        }
    }

    void ApplicationSpecs::prune_decision_trees(const double best_error) const {
        for (const auto& hostname : _hostnames) {
            this->prune_decision_tree(hostname, best_error);
        }
    }

    void ApplicationSpecs::clear_decision_trees() const {
        for (const auto& hostname : _hostnames) {
            this->clear_decision_tree(hostname);
        }
    }

    void ApplicationSpecs::build_decision_tree(const std::string& hostname) const {
        _hosts_decision_trees.at(hostname)->build_tree();
    }

    void ApplicationSpecs::prune_decision_tree(const std::string& hostname, const double best_error) const {
        _hosts_decision_trees.at(hostname)->prune_tree(best_error);
    }

    void ApplicationSpecs::clear_decision_tree(const std::string& hostname) const {
        _hosts_decision_trees.at(hostname)->prune_tree(0.0);
    }

    bool ApplicationSpecs::decision_tree_empty(const std::string& hostname) const {
        std::function<void(const std::shared_ptr<ExecOptionDecisionNode>&, int)> print_tree = [&](const std::shared_ptr<ExecOptionDecisionNode>& node, int depth) {
            if (!node) return;

            std::string indent(depth * 2, ' ');

            std::cout << indent
                      << "execution_option: " << node->execution_option
                      << " | task: " << node->task
                      << " | error_lvl: " << node->cumulative_error_factor
                      << std::endl;

            for (const auto& child : node->children) {
                print_tree(child, depth + 1);
            }
        };
        print_tree(_hosts_decision_trees.at(hostname)->root, 0);

        if (_hosts_decision_trees.at(hostname)->root == nullptr || _hosts_decision_trees.at(hostname)->root->num_children == 0) {
            return true;
        }
        return false;
    }

    ////////////////// DECISION TREE INTERNAL METHODS //////////////////

    void ApplicationSpecs::ExecOptionDecisionTree::build_tree() const {
        if (application_specs->_num_tasks == 0) {
            std::cerr << "No tasks!" << std::endl;
            return;
        }

        build_tree_helper(0, root, 1.0, 1.0);
    }

    void ApplicationSpecs::ExecOptionDecisionTree::build_tree_helper(const int task_index,
                                                                     const std::shared_ptr<ExecOptionDecisionNode>& parent,
                                                                     const double running_data_size_factor,
                                                                     const double running_error_factor) const {

        if (task_index >= application_specs->_num_tasks) {
            parent->is_leaf = true;
            return;
        }

        const std::string& task_name = application_specs->_task_order[task_index];
        for (const auto& [option_name, functions] : application_specs->_task_functions.at(task_name)) {
            auto child = ExecOptionDecisionNode::create_decision_node(task_name, option_name, false);

            const double current_error_factor = functions.at("e_function")(0, running_error_factor);
            child->cumulative_error_factor = current_error_factor;

            const double current_data_size_factor = functions.at("d_function")(0, running_data_size_factor);
            child->cumulative_data_size_factor = current_data_size_factor;

            parent->children.push_back(child);
            parent->num_children++;

            build_tree_helper(task_index + 1, child, current_data_size_factor, current_error_factor);
        }
    }

    void ApplicationSpecs::ExecOptionDecisionTree::prune_tree(const double best_error) const {
        prune_tree_helper(root, best_error);
    }

    bool ApplicationSpecs::ExecOptionDecisionTree::prune_tree_helper(const std::shared_ptr<ExecOptionDecisionNode>& node, const double best_error) const {
        if (!node) {
            return true;
        }

        // If this is a leaf node
        if (node->children.empty()) {
            return node->cumulative_error_factor > best_error;
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
