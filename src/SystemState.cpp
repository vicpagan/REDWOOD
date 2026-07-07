#include "SystemState.h"

#include <iostream>

namespace wrench {

    void HostState::reset() {
        current_task.clear();
        current_exec_option.clear();
        current_task_start_time = 0.0;
        is_down = false;
        if (is_finished) {
            std::cerr << "RESETTING HOST THAT HAS FINISHED!!!\n";
        }
        is_finished = false;
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
        std::cout << "tracking job for host " << hostname << std::endl;
    }

    void SystemState::untrack_job(const std::string& hostname) {
        std::cout << "untracking job for host " << hostname << std::endl;
        _jobs_to_hosts[hostname] = nullptr;
    }

    std::shared_ptr<CompoundJob> SystemState::get_running_job(const std::string& hostname) {
        return _jobs_to_hosts[hostname];
    }

    const std::string& SystemState::get_host_current_task(const std::string& hostname) {
        if (_host_states.find(hostname) == _host_states.end()) {
            throw std::invalid_argument("Hostname '" + hostname + "' not found in hosts");
        }
        return _host_states[hostname].current_task;
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
        std::cout << "resetting host " << hostname << std::endl;
        _host_states[hostname].reset();
    }

    void SystemState::reset_all_hosts() {
        for (auto&[hostname, host_state] : _host_states) {
            // std::cout << "resetting host " << hostname << std::endl;
            host_state.reset();
        }
    }

    void SystemState::set_host_down(const std::string& hostname) {
        // std::cout << "setting host down for " << hostname << std::endl;
        _host_states[hostname].is_down = true;
    }

    void SystemState::set_host_up(const std::string& hostname) {
        // std::cout << "setting host up for " << hostname << std::endl;
        _host_states[hostname].is_down = false;
    }

    void SystemState::set_host_finished(const std::string& hostname) {
        _host_states[hostname].is_finished = true;
    }

    bool SystemState::is_host_idle(const std::string& hostname) const {
        if (_host_states.find(hostname) == _host_states.end()) {
            throw std::invalid_argument("Hostname '" + hostname + "' not found in hosts");
        }
        return _host_states.at(hostname).is_idle();
    }

    bool SystemState::is_host_down(const std::string& hostname) const {
        if (_host_states.find(hostname) == _host_states.end()) {
            throw std::invalid_argument("Hostname '" + hostname + "' not found in hosts");
        }
        return _host_states.at(hostname).is_down;
    }

    bool SystemState::is_host_finished(const std::string& hostname) const {
        if (_host_states.find(hostname) == _host_states.end()) {
            throw std::invalid_argument("Hostname '" + hostname + "' not found in hosts");
        }
        return _host_states.at(hostname).is_finished;
    }

    bool SystemState::are_all_hosts_finished() const {
        bool all_hosts_finished = true;
        for (const auto& [hostname, host_state] : _host_states) {
            if (!host_state.is_finished) {
                all_hosts_finished = false;
            }
        }
        return all_hosts_finished;
    }

};
