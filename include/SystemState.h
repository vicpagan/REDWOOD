#ifndef SYSTEMSTATE_H
#define SYSTEMSTATE_H

#include <map>
#include <vector>
#include <string>
#include <utility>

namespace wrench {

    class CompoundJob;

    struct HostState {
        std::string current_task;
        std::string current_exec_option;
        double current_task_start_time;
        bool is_down;

        HostState() : current_task_start_time(0.0), is_down(false) {}

        void reset();
        bool is_idle() const;
    };

    class SystemState {
    public:

        // Job tracking management
         void track_job(
            const std::shared_ptr<CompoundJob>& compound_job,
            const std::string& hostname,
            const std::string& task_name,
            const std::string& execution_option,
            double start_time);

         // void untrack_job(const std::shared_ptr<CompoundJob>& compound_job);
         void untrack_job(const std::string& hostname);

         bool is_a_job_running(const std::string& hostname);
         // std::shared_ptr<RunningJob> get_running_job(const std::shared_ptr<CompoundJob>& compound_job);
         std::shared_ptr<CompoundJob> get_running_job(const std::string& hostname);

         static std::shared_ptr<SystemState> create_tracker(const std::vector<std::string>& hostnames) {
           return std::shared_ptr<SystemState>(new SystemState(hostnames));
         }

        // Host state management
        void reset_host(const std::string& hostname);
        void reset_all_hosts();

        void set_host_down(const std::string& hostname);
        void set_host_up(const std::string& hostname);

        bool is_host_idle(const std::string& hostname) const;
        bool is_host_down(const std::string& hostname) const;

        // Iterable interface
        auto begin() { return _jobs_to_hosts.begin(); }
        auto end()   { return _jobs_to_hosts.end(); }
        auto begin() const { return _jobs_to_hosts.begin(); }
        auto end()   const { return _jobs_to_hosts.end(); }

    private:
        explicit SystemState(const std::vector<std::string>& hostnames);

        std::map<std::string, std::shared_ptr<CompoundJob>> _jobs_to_hosts;
        std::map<std::string, HostState> _host_states;
    };

    inline bool SystemState::is_a_job_running(const std::string& hostname) {
        return _jobs_to_hosts[hostname] != nullptr;
    }

};


#endif //SYSTEMSTATE_H
