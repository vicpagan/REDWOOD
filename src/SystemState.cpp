#include "SystemState.h"

#include <iostream>

namespace wrench {

    void HostState::reset() {
        current_task.clear();
        current_exec_option.clear();
        current_task_start_time = 0.0;
        is_down = false;
    }

    bool HostState::is_idle() const {
        return current_task.empty() && !is_down;
    }

    void SystemState::track_job(const std::shared_ptr<CompoundJob>& compound_job,
                               const std::string& hostname,
                               const std::string& task_name,
                               const std::string& execution_option,
                               const double start_time) {
        _jobs_to_hosts[hostname] = compound_job;
        _host_states[hostname].current_task = task_name;
        _host_states[hostname].current_exec_option = execution_option;
        _host_states[hostname].current_task_start_time = start_time;
    }

    // void SystemState::untrack_job(const std::shared_ptr<CompoundJob>& compound_job) {
    //
    // }

    void SystemState::untrack_job(const std::string& hostname) {
        _jobs_to_hosts[hostname] = nullptr;
    }


    // std::shared_ptr<RunningJob> SystemState::get_running_job(const std::shared_ptr<CompoundJob>& compound_job) {
    // }

    std::shared_ptr<CompoundJob> SystemState::get_running_job(const std::string& hostname) {
        return _jobs_to_hosts[hostname];
    }

    SystemState::SystemState(const std::vector<std::string>& hostnames) {
        for (const auto& hostname : hostnames) {
            _host_states[hostname] = HostState();
            _jobs_to_hosts[hostname] = nullptr;
        }
    }

    void SystemState::reset_host(const std::string& hostname) {
        if (_host_states.find(hostname) == _host_states.end()) {
            throw std::invalid_argument("Hostname '" + hostname + "' not found in hosts");
        }
        _host_states[hostname].reset();
    }

    void SystemState::reset_all_hosts() {
        for (auto&[hostname, host_state] : _host_states) {
            host_state.reset();
        }
    }

    void SystemState::set_host_down(const std::string& hostname) {
        _host_states[hostname].is_down = true;
    }

    void SystemState::set_host_up(const std::string& hostname) {
        _host_states[hostname].is_down = false;
    }

    void SystemState::update_host_decision_node(const std::string& hostname, const std::string& task_name, const std::string& execution_option) {
        const ApplicationSpecs::ExecOptionDecisionNode* decision_node = _host_states[hostname].current_decision_node;
        for (auto child : decision_node->children) {
            if (child->task == task_name && child->execution_option == execution_option) {
                decision_node = child.get();
                break;
            }
        }
        if (_host_states.find(hostname) == _host_states.end()) {
            throw std::invalid_argument("Hostname '" + hostname + "' not found in hosts");
        }
        _host_states[hostname].current_decision_node = decision_node;
    }

    void SystemState::update_all_hosts_decision_nodes(const std::string& success_hostname, const std::string &task_name, const std::string &execution_option) {
        const ApplicationSpecs::ExecOptionDecisionNode* decision_node = _host_states[success_hostname].current_decision_node;
        for (auto child : decision_node->children) {
            if (child->task == task_name && child->execution_option == execution_option) {
                decision_node = child.get();
                break;
            }
        }
        for (auto& [hostname, host_state] : _host_states) {
            host_state.current_decision_node = decision_node;
        }
    }

    void SystemState::initialize_all_hosts_decision_nodes(const ApplicationSpecs::ExecOptionDecisionNode* root_node) {
        for (auto& [hostname, host_state] : _host_states) {
            host_state.current_decision_node = root_node;
        }
    }

    const ApplicationSpecs::ExecOptionDecisionNode* SystemState::get_host_current_decision_node(const std::string& hostname) const {
        if (_host_states.find(hostname) == _host_states.end()) {
            throw std::invalid_argument("Hostname '" + hostname + "' not found in hosts");
        }
        return _host_states.find(hostname)->second.current_decision_node;
    }

    bool SystemState::is_host_idle(const std::string& hostname) const {
        if (_host_states.find(hostname) == _host_states.end()) {
            throw std::invalid_argument("Hostname '" + hostname + "' not found in hosts");
        }
        return _host_states.find(hostname)->second.is_idle();
    }

    bool SystemState::is_host_down(const std::string& hostname) const {
        if (_host_states.find(hostname) == _host_states.end()) {
            throw std::invalid_argument("Hostname '" + hostname + "' not found in hosts");
        }
        return _host_states.find(hostname)->second.is_down;
    }

};
