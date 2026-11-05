#ifndef SYSTEMSTATE_H
#define SYSTEMSTATE_H

#include <map>
#include <vector>
#include <string>
#include <utility>

#include "ApplicationSpecs.h"

namespace wrench {

    class CompoundJob;

    struct HostState {
        std::string current_task;
        std::string current_exec_option;
        double current_task_start_time;
        bool is_down;
        bool is_finished;

        /**
         * @brief Constructor: a host with no task, up and not finished
         */
        HostState() : current_task_start_time(0.0), is_down(false), is_finished(false) {}

        void reset();

        bool is_idle() const;
    };

    class SystemState {
    public:

        // job tracking management
         void track_job(
            const std::shared_ptr<CompoundJob>& compound_job,
            const std::string& hostname,
            const std::string& task_name,
            const std::string& execution_option,
            double start_time);

         void untrack_job(const std::string& hostname);

         bool is_a_job_running(const std::string& hostname);
         std::shared_ptr<CompoundJob> get_running_job(const std::string& hostname);
         const std::string& get_host_current_task(const std::string& hostname);

         /**
          * @brief Create a system state tracker
          * @param hostnames The names of the compute hosts to track
          * @return A shared pointer to the new tracker
          */
         static std::shared_ptr<SystemState> create_tracker(const std::vector<std::string>& hostnames) {
           return std::shared_ptr<SystemState>(new SystemState(hostnames));
         }

        // host state management
        void reset_host(const std::string& hostname);
        void reset_all_hosts();

        void set_host_down(const std::string& hostname);
        void set_host_up(const std::string& hostname);
        void set_host_finished(const std::string& hostname);

        bool is_host_idle(const std::string& hostname) const;
        bool is_host_down(const std::string& hostname) const;
        bool is_host_finished(const std::string& hostname) const;
        bool are_all_hosts_finished() const;

        // iterable interface
        /**
         * @brief Get an iterator to the first (hostname, running job) entry, in hostname order
         * @return The iterator
         */
        auto begin() { return _jobs_to_hosts.begin(); }
        /**
         * @brief Get the end iterator of the (hostname, running job) entries
         * @return The iterator
         */
        auto end()   { return _jobs_to_hosts.end(); }
        /**
         * @brief Get a const iterator to the first (hostname, running job) entry, in hostname order
         * @return The iterator
         */
        auto begin() const { return _jobs_to_hosts.begin(); }
        /**
         * @brief Get the const end iterator of the (hostname, running job) entries
         * @return The iterator
         */
        auto end()   const { return _jobs_to_hosts.end(); }

    private:
        explicit SystemState(const std::vector<std::string>& hostnames);

        std::map<std::string, std::shared_ptr<CompoundJob>> _jobs_to_hosts;
        std::map<std::string, HostState> _host_states;
    };

    /**
     * @brief Determine whether a job is tracked as running on a host
     * @param hostname The name of the host
     * @return true if a job is tracked on the host
     */
    inline bool SystemState::is_a_job_running(const std::string& hostname) {
        return _jobs_to_hosts[hostname] != nullptr;
    }

};


#endif //SYSTEMSTATE_H
