#ifndef REDWOOD_APPLICATIONSPECS_H
#define REDWOOD_APPLICATIONSPECS_H

#include <map>
#include <random>
#include <boost/json/object.hpp>
#include <utility>
#include <memory>
#include <variant>


namespace wrench {

    class ApplicationSpecs {
    public:

        ApplicationSpecs(const boost::json::object& platform_spec,
            const boost::json::object& failure_spec,
            const boost::json::object& application_spec,
            const boost::json::object& execution_spec,
            const boost::json::object& scheduling_spec);

        static std::shared_ptr<ApplicationSpecs> create_application_specs(
            const boost::json::object& platform_spec,
            const boost::json::object& failure_spec,
            const boost::json::object& application_spec,
            const boost::json::object& execution_spec,
            const boost::json::object& scheduling_spec);


        friend struct ExecOptionDecisionNode;
        friend struct ExecOptionDecisionTree;

        struct ExecOptionDecisionNode {
            std::vector<std::shared_ptr<ExecOptionDecisionNode>> children;
            std::string task;
            std::string execution_option;

            int num_children;
            bool is_leaf;
            double cumulative_data_size_factor;
            double cumulative_error_factor;

            ExecOptionDecisionNode(std::string task, std::string execution_option, const bool is_leaf) :
                task(std::move(task)),
                execution_option(std::move(execution_option)),
                num_children(0),
                is_leaf(is_leaf),
                cumulative_data_size_factor(1.0),
                cumulative_error_factor(1.0) {
            }

            static std::shared_ptr<ExecOptionDecisionNode> create_decision_node(std::string task, std::string execution_option, const bool is_leaf) {
                return std::make_shared<ExecOptionDecisionNode>(std::move(task), std::move(execution_option), is_leaf);
            }
        };

        struct ExecOptionDecisionTree {
            ApplicationSpecs* application_specs;
            std::shared_ptr<ExecOptionDecisionNode> root;

            explicit ExecOptionDecisionTree(ApplicationSpecs* application_specs, std::shared_ptr<ExecOptionDecisionNode> root) :
                application_specs(application_specs),
                root(std::move(root)) {
            }

            void build_tree(const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options);

            void build_tree_helper(const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
                int task_index,
                const std::shared_ptr<ExecOptionDecisionNode>& parent,
                double running_data_size_factor,
                double running_error_factor);

            void prune_tree(double best_error);

            bool prune_tree_helper(const std::shared_ptr<ExecOptionDecisionNode>& node, double best_error);
        };

        void build_decision_tree(const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options) const;
        void prune_decision_tree(double best_error) const;
        bool decision_tree_empty() const;

        ExecOptionDecisionNode* get_decision_tree_root() const { return _exec_option_decision_tree->root.get(); }

        int get_num_compute_nodes() const { return _num_compute_nodes; }
        double get_e_fail() const { return _e_fail; }
        double get_io_read_bandwidth_per_node() const { return _io_read_bandwidth_per_node; }
        double get_io_write_bandwidth_per_node() const { return _io_write_bandwidth_per_node; }
        double get_restart_overhead() const { return _restart_overhead; }
        double get_deadline() const { return _deadline; }
        double get_lambda() const { return _lambda; }
        std::string get_delta_t_scheme() const { return _delta_t_scheme; }
        double get_delta_t_parameter() const { return _delta_t_parameter; }
        std::exponential_distribution<double> get_exponential_distribution() const { return _exponential_distribution; }
        int get_seed() const { return _seed; }

        double get_initial_data_size() const { return _initial_data_size; }
        double get_initial_error_level() const { return _initial_error_level; }

        std::string get_task(int index);

    protected:
        int _num_compute_nodes;
        double _io_read_bandwidth_per_node;
        double _io_write_bandwidth_per_node;
        double _deadline;
        double _restart_overhead;
        double _e_fail;
        double _lambda;
        std::string _delta_t_scheme;
        double _delta_t_parameter;
        std::exponential_distribution<double> _exponential_distribution;
        int _seed;

        double _initial_data_size;
        double _initial_error_level;

        std::vector<std::string> _task_order;
        int _num_tasks;
        std::shared_ptr<ExecOptionDecisionTree> _exec_option_decision_tree;
    };
}

#endif //REDWOOD_APPLICATIONSPECS_H
