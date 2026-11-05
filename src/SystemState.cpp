#include "SystemState.h"

#include <iostream>

namespace wrench {

    /**
     * @brief Clear the host's task, execution option and start time, and mark it as up and not finished
     *        (prints a warning if it was finished)
     */
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

    /**
     * @brief Determine whether the host can be given a task
     * @return true if the host has no task and is not down
     */
    bool HostState::is_idle() const {
        return current_task.empty() && !is_down;
    }

    /**
     * @brief Record that a job has been submitted to a host
     * @param compound_job The job
     * @param hostname The name of the host
     * @param task_name The name of the task that the job executes
     * @param execution_option The name of the execution option that the job executes
     * @param start_time The date at which the job was submitted
     */
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

    /**
     * @brief Forget the job of a host (the host's task information is not cleared, see reset_host())
     * @param hostname The name of the host
     */
    void SystemState::untrack_job(const std::string& hostname) {
        _jobs_to_hosts[hostname] = nullptr;
    }

    /**
     * @brief Get the job tracked on a host
     * @param hostname The name of the host
     * @return The job, or nullptr if there is none
     */
    std::shared_ptr<CompoundJob> SystemState::get_running_job(const std::string& hostname) {
        return _jobs_to_hosts[hostname];
    }

    /**
     * @brief Get the name of the task that a host is executing
     * @param hostname The name of the host (throws std::invalid_argument if unknown)
     * @return The task name ("" if none)
     */
    const std::string& SystemState::get_host_current_task(const std::string& hostname) {
        if (_host_states.find(hostname) == _host_states.end()) {
            throw std::invalid_argument("Hostname '" + hostname + "' not found in hosts");
        }
        return _host_states[hostname].current_task;
    }

    /**
     * @brief Constructor: every host has no job and no task, is up and not finished
     * @param hostnames The names of the compute hosts to track
     */
    SystemState::SystemState(const std::vector<std::string>& hostnames) {
        for (const auto& hostname : hostnames) {
            _host_states[hostname] = HostState();
            _jobs_to_hosts[hostname] = nullptr;
        }
    }

    /**
     * @brief Reset a host's state (see HostState::reset()): this also marks it as up and not finished
     * @param hostname The name of the host (throws std::invalid_argument if unknown)
     */
    void SystemState::reset_host(const std::string& hostname) {
        if (_host_states.find(hostname) == _host_states.end()) {
            throw std::invalid_argument("Hostname '" + hostname + "' not found in hosts");
        }
        _host_states[hostname].reset();
    }

    /**
     * @brief Reset every host's state (see HostState::reset())
     */
    void SystemState::reset_all_hosts() {
        for (auto&[hostname, host_state] : _host_states) {
            // std::cout << "resetting host " << hostname << std::endl;
            host_state.reset();
        }
    }

    /**
     * @brief Mark a host as down
     * @param hostname The name of the host
     */
    void SystemState::set_host_down(const std::string& hostname) {
        // std::cout << "setting host down for " << hostname << std::endl;
        _host_states[hostname].is_down = true;
    }

    /**
     * @brief Mark a host as up
     * @param hostname The name of the host
     */
    void SystemState::set_host_up(const std::string& hostname) {
        // std::cout << "setting host up for " << hostname << std::endl;
        _host_states[hostname].is_down = false;
    }

    /**
     * @brief Mark a host as finished (it takes no part in the rest of the repetition)
     * @param hostname The name of the host
     */
    void SystemState::set_host_finished(const std::string& hostname) {
        _host_states[hostname].is_finished = true;
    }

    /**
     * @brief Determine whether a host can be given a task (see HostState::is_idle())
     * @param hostname The name of the host (throws std::invalid_argument if unknown)
     * @return true if the host has no task and is not down
     */
    bool SystemState::is_host_idle(const std::string& hostname) const {
        if (_host_states.find(hostname) == _host_states.end()) {
            throw std::invalid_argument("Hostname '" + hostname + "' not found in hosts");
        }
        return _host_states.at(hostname).is_idle();
    }

    /**
     * @brief Determine whether a host is down
     * @param hostname The name of the host (throws std::invalid_argument if unknown)
     * @return true if the host is down
     */
    bool SystemState::is_host_down(const std::string& hostname) const {
        if (_host_states.find(hostname) == _host_states.end()) {
            throw std::invalid_argument("Hostname '" + hostname + "' not found in hosts");
        }
        return _host_states.at(hostname).is_down;
    }

    /**
     * @brief Determine whether a host is finished
     * @param hostname The name of the host (throws std::invalid_argument if unknown)
     * @return true if the host is finished
     */
    bool SystemState::is_host_finished(const std::string& hostname) const {
        if (_host_states.find(hostname) == _host_states.end()) {
            throw std::invalid_argument("Hostname '" + hostname + "' not found in hosts");
        }
        return _host_states.at(hostname).is_finished;
    }

    /**
     * @brief Determine whether every host is finished
     * @return true if every host is finished
     */
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
