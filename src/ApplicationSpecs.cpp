#include "ApplicationSpecs.h"
#include <wrench/util/UnitParser.h>

#include <iostream>
#include <boost/json/value_to.hpp>
#include <stack>

#include "FunctionGenerator.h"
#include "Utils.h"

namespace wrench {

    /**
     * @brief Constructor: parse the JSON specification, create the task functions, merge in situ tasks, and
     *        create an (unbuilt) decision tree for each host, with each host starting at the first task
     * @param platform_spec The "platform" JSON object (number of compute nodes, I/O bandwidths)
     * @param failure_spec The "failures" JSON object (lambda, restart overhead, seed)
     * @param application_spec The "application" JSON object (tasks and their execution options, initial data size and error level)
     * @param execution_spec The "execution" JSON object (deadline, e_fail)
     * @param scheduling_spec The "scheduling" JSON object (delta_t scheme)
     * @param hostnames The names of the compute hosts (may be empty)
     */
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

    /**
     * @brief Create an application specification (see the constructor)
     * @param platform_spec The "platform" JSON object
     * @param failure_spec The "failures" JSON object
     * @param application_spec The "application" JSON object
     * @param execution_spec The "execution" JSON object
     * @param scheduling_spec The "scheduling" JSON object
     * @param hostnames The names of the compute hosts (may be empty)
     * @return A shared pointer to the new application specification
     */
    std::shared_ptr<ApplicationSpecs> ApplicationSpecs::create_application_specs(
        const boost::json::object& platform_spec,
        const boost::json::object& failure_spec,
        const boost::json::object& application_spec,
        const boost::json::object& execution_spec,
        const boost::json::object& scheduling_spec,
        const std::vector<std::string>& hostnames) {

        return std::make_shared<ApplicationSpecs>(platform_spec, failure_spec, application_spec, execution_spec, scheduling_spec, hostnames);
    }

    /**
     * @brief Merge each run of consecutive tasks whose in_situ_with_next_task flag is set (together with the task that
     *        follows them) into a single task, named "task1+task2+...", whose options are all combinations of the
     *        merged tasks' options, named "opt1+opt2+...", with composed t, d and e functions. Updates the task order
     *        and the number of tasks
     * @param task_functions The task functions to update in place (task name -> option name -> function name -> function)
     */
    void ApplicationSpecs::merge_in_situ_tasks(std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& task_functions) {
        int i = 0;
        while (i < _num_tasks) {
            std::string task_name = get_task(i);
            if (!_in_situ_tasks.at(task_name)) {
                i++;
                continue;
            }

            // collect the tasks to merge
            int num_tasks_to_merge = 1;

            std::vector<std::string> tasks_to_merge;
            tasks_to_merge.push_back(get_task(i));

            while (_in_situ_tasks.at(tasks_to_merge.back()) && i + tasks_to_merge.size() < _num_tasks) {
                tasks_to_merge.push_back(get_task(i + static_cast<int>(tasks_to_merge.size())));
                num_tasks_to_merge++;
            }

            // need to cover edge case of last task having the in_situ_with_next_task flag set to true (for some stupid reason just in case I guess)
            if (num_tasks_to_merge == 1) {
                i++;
                continue;
            }

            // merge the list of tasks into a single task
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

            // update _task_functions with merged tasks and update application_specs
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

    /**
     * @brief Set the data size of the input of a host's next task
     * @param hostname The name of the host
     * @param data_size The data size
     */
    void ApplicationSpecs::update_host_running_data_size(const std::string& hostname, const double data_size) {
        _hosts_running_data_size_and_error_level[hostname].first = data_size;
    }

    /**
     * @brief Set the error level of the input of a host's next task
     * @param hostname The name of the host
     * @param error_level The error level
     */
    void ApplicationSpecs::update_host_running_error_level(const std::string& hostname, const double error_level) {
        _hosts_running_data_size_and_error_level[hostname].second = error_level;
    }

    /**
     * @brief Get the data size of the input of a host's next task
     * @param hostname The name of the host
     * @return The data size
     */
    double ApplicationSpecs::get_host_running_data_size(const std::string& hostname) const {
        return _hosts_running_data_size_and_error_level.at(hostname).first;
    }

    /**
     * @brief Get the error level of the input of a host's next task
     * @param hostname The name of the host
     * @return The error level
     */
    double ApplicationSpecs::get_host_running_error_level(const std::string& hostname) const {
        return _hosts_running_data_size_and_error_level.at(hostname).second;
    }

    /**
     * @brief Set a host's next task, by index
     * @param hostname The name of the host
     * @param task_index The index of the task in the task order (throws std::out_of_range if invalid)
     */
    void ApplicationSpecs::update_host_task_to_schedule(const std::string& hostname, const int task_index) {
        _hosts_current_scheduled_tasks[hostname] = _task_order.at(task_index);
    }

    /**
     * @brief Set a host's next task, by name
     * @param hostname The name of the host
     * @param task_name The name of the task ("" if the host has no task left)
     */
    void ApplicationSpecs::update_host_task_to_schedule(const std::string& hostname, const std::string& task_name) {
        _hosts_current_scheduled_tasks[hostname] = task_name;
    }

    /**
     * @brief Move a host on to the task that follows its current one, or to "" after the last task
     * @param hostname The name of the host (its current task must be a task of the chain, else std::out_of_range)
     */
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

    /**
     * @brief Get the name of a host's next task
     * @param hostname The name of the host
     * @return The task name ("" if the host has no task left)
     */
    std::string ApplicationSpecs::get_host_task_to_schedule(const std::string& hostname) const {
        return _hosts_current_scheduled_tasks.at(hostname);
    }

    /**
     * @brief Get the index of a host's next task in the task order
     * @param hostname The name of the host
     * @return The task index (see get_task_index())
     */
    int ApplicationSpecs::get_host_task_to_schedule_index(const std::string& hostname) const {
        std::string current_task = _hosts_current_scheduled_tasks.at(hostname);
//        std::cerr << "Host " << hostname << " current task: " << current_task << std::endl;
        return get_task_index(_hosts_current_scheduled_tasks.at(hostname));
    }

    /**
     * @brief Get the name of a task from its index in the task order
     * @param index The task index
     * @return The task name, or "" if the index is out of range
     */
    std::string ApplicationSpecs::get_task(const int index) const {
        if (index < 0 || index >= _num_tasks) {
            return "";
        }
        return _task_order.at(index);
    }

    /**
     * @brief Get the index of a task in the task order
     * @param task_name The task name
     * @return The task index (a name that is not in the task order, including "", makes .at() throw
     *         std::out_of_range before -1 can be returned)
     */
    int ApplicationSpecs::get_task_index(const std::string& task_name) const {
//        std::cerr << "Getting task index for task " << task_name << std::endl;
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

    /**
     * @brief Get a host's current node in its decision tree (the node of the last option it executed)
     * @param hostname The name of the host
     * @return The current decision node
     */
    const ApplicationSpecs::ExecOptionDecisionNode* ApplicationSpecs::get_host_current_decision_node(const std::string& hostname) const {
        return _hosts_current_decision_nodes.at(hostname).get();
    }

    /**
     * @brief Move a host to the child of its current decision node that matches a task and execution option, and
     *        add the option to the host's decision history (prints a message instead if the current node is a leaf).
     *        If no child matches, the host stays at its current node, but the option is still added to the history
     * @param hostname The name of the host
     * @param task_name The name of the task that was executed
     * @param execution_option The name of the execution option that was executed
     */
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

    /**
     * @brief Set a host's current decision node to the node, in its own decision tree, reached by following another
     *        host's decision history from the root, and copy that host's decision history
     * @param hostname The name of the host to update
     * @param reference_hostname The name of the host whose decision history is followed (throws std::runtime_error if
     *        an option of that history is not in the host's tree)
     */
    void ApplicationSpecs::update_host_current_decision_node(const std::string& hostname, const std::string& reference_hostname) {
        auto current_decision_node = _hosts_decision_trees.at(hostname)->root;
        for (const auto& entry : _hosts_decision_history[reference_hostname]) {
            bool found = false;
            for (const auto& child : current_decision_node->children) {
                if (child->execution_option == entry) {
                    current_decision_node = child;
                    found = true;
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
        _hosts_decision_history[hostname] = _hosts_decision_history.at(reference_hostname);
    }

    /**
     * @brief Determine whether the best final error level reachable from a host's current decision node is at least as
     *        good as the worst one reachable from another host's current decision node (comparison with tolerance, see
     *        is_strictly_better())
     * @param hostname The name of the host
     * @param reference_hostname The name of the reference host
     * @return true unless the reference's worst leaf error level is strictly better than the host's best one
     */
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

        bool result = !is_strictly_better(max_error_factor, min_error_factor);

        return result;
    }

    /**
     * @brief Set a host's current decision node back to the root of its decision tree
     * @param hostname The name of the host
     */
    void ApplicationSpecs::reset_host_current_decision_node(const std::string& hostname) {
        _hosts_current_decision_nodes[hostname] = _hosts_decision_trees.at(hostname)->root;
    }

    /**
     * @brief Set every host's current decision node back to the root of its decision tree
     */
    void ApplicationSpecs::reset_all_hosts_current_decision_nodes() {
        for (const auto& hostname : _hostnames) {
            _hosts_current_decision_nodes[hostname] = _hosts_decision_trees.at(hostname)->root;
        }
    }

    /**
     * @brief Copy another host's decision history (the options executed so far) to a host
     * @param hostname The name of the host to update
     * @param reference_hostname The name of the host whose history is copied
     */
    void ApplicationSpecs::update_host_decision_history(const std::string& hostname, const std::string& reference_hostname) {
        _hosts_decision_history[hostname] = _hosts_decision_history.at(reference_hostname);
    }

    /**
     * @brief Clear a host's decision history
     * @param hostname The name of the host
     */
    void ApplicationSpecs::reset_host_decision_history(const std::string& hostname) {
        _hosts_decision_history[hostname].clear();
    }

    /**
     * @brief Clear every host's decision history
     */
    void ApplicationSpecs::reset_all_hosts_decision_history() {
        for (const auto& hostname : _hostnames) {
            _hosts_decision_history[hostname].clear();
        }
    }

    ////////////////// DECISION TREE MANAGEMENT METHODS //////////////////
    
    /**
     * @brief Build every host's decision tree (see ExecOptionDecisionTree::build_tree())
     */
    void ApplicationSpecs::build_decision_trees() const {
        for (const auto& hostname : _hostnames) {
            this->build_decision_tree(hostname);
        }
    }

    /**
     * @brief Prune every host's decision tree (see ExecOptionDecisionTree::prune_tree())
     * @param best_error The error level to improve on
     */
    void ApplicationSpecs::prune_decision_trees(const double best_error) const {
        for (const auto& hostname : _hostnames) {
            this->prune_decision_tree(hostname, best_error);
        }
    }

    /**
     * @brief Clear every host's decision tree (see clear_decision_tree())
     */
    void ApplicationSpecs::clear_decision_trees() const {
        for (const auto& hostname : _hostnames) {
            this->clear_decision_tree(hostname);
        }
    }

    /**
     * @brief Build a host's decision tree (see ExecOptionDecisionTree::build_tree())
     * @param hostname The name of the host
     */
    void ApplicationSpecs::build_decision_tree(const std::string& hostname) const {
        _hosts_decision_trees.at(hostname)->build_tree();
    }

    /**
     * @brief Prune a host's decision tree (see ExecOptionDecisionTree::prune_tree())
     * @param hostname The name of the host
     * @param best_error The error level to improve on
     */
    void ApplicationSpecs::prune_decision_tree(const std::string& hostname, const double best_error) const {
        _hosts_decision_trees.at(hostname)->prune_tree(best_error);
    }

    /**
     * @brief Clear a host's decision tree, by pruning it with an error level of 0.0: every leaf whose error level is
     *        not strictly below 0 is removed (error levels are not negative), so only the root remains
     * @param hostname The name of the host
     */
    void ApplicationSpecs::clear_decision_tree(const std::string& hostname) const {
        _hosts_decision_trees.at(hostname)->prune_tree(0.0);
    }

    /**
     * @brief Determine whether a host's decision tree is empty
     * @param hostname The name of the host
     * @return true if the tree has no root or its root has no children
     */
    bool ApplicationSpecs::decision_tree_empty(const std::string& hostname) const {
        if (_hosts_decision_trees.at(hostname)->root == nullptr || _hosts_decision_trees.at(hostname)->root->num_children == 0) {
            return true;
        }
        return false;
    }

    ////////////////// DECISION TREE INTERNAL METHODS //////////////////

    /**
     * @brief Build the tree below the root: one level per task, one child per execution option (prints a message and
     *        does nothing if there are no tasks)
     */
    void ApplicationSpecs::ExecOptionDecisionTree::build_tree() const {
        if (application_specs->_num_tasks == 0) {
            std::cerr << "No tasks!" << std::endl;
            return;
        }

        build_tree_helper(0, root, 1.0, 1.0);
    }

    /**
     * @brief Recursively add, below a node, one child per execution option of a task, and the subtrees for the
     *        following tasks. Each child stores its cumulative data size and error factors, computed as
     *        d(0, parent factor) and e(0, parent factor). Marks the node as a leaf when there is no task left
     * @param task_index The index of the task whose options become the node's children
     * @param parent The node below which to add the children
     * @param running_data_size_factor The parent's cumulative data size factor
     * @param running_error_factor The parent's cumulative error factor
     */
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

    /**
     * @brief Remove every leaf whose error level is not strictly better than a given error level, then every node
     *        left without children (the root itself is never removed)
     * @param best_error The error level to improve on
     */
    void ApplicationSpecs::ExecOptionDecisionTree::prune_tree(const double best_error) const {
        prune_tree_helper(root, best_error);
    }

    /**
     * @brief Recursively prune the subtree of a node (see prune_tree())
     * @param node The root of the subtree
     * @param best_error The error level to improve on
     * @return true if the node itself should be removed: a null node, a leaf whose error level is not strictly
     *         better than best_error, or a node whose children were all removed
     */
    bool ApplicationSpecs::ExecOptionDecisionTree::prune_tree_helper(const std::shared_ptr<ExecOptionDecisionNode>& node, const double best_error) const {
        if (!node) {
            return true;
        }

        // if this is a leaf node
        if (node->children.empty()) {
            return !is_strictly_better(node->cumulative_error_factor, best_error);
        }

        // otherwise, recursively prune children
        auto& children = node->children;
        for (auto child_iterator = children.begin(); child_iterator != children.end(); ) {
            if (prune_tree_helper(*child_iterator, best_error)) {
                child_iterator = children.erase(child_iterator); // remove child
                node->num_children--;
            } else {
                ++child_iterator;
            }
        }

        // if after pruning all children are gone, prune this node as well
        return children.empty();
    }

}
