#ifndef RUNNINGJOBTRACKER_H
#define RUNNINGJOBTRACKER_H

#include <map>
#include <string>
#include <utility>

namespace wrench {

    class CompoundJob;
    class RunningJob;
    
    class JobTracker {
      
    public:
         void track_job(
            const std::shared_ptr<CompoundJob>& compound_job,
            const std::string& hostname,
            const std::string& task_name,
            const std::string& execution_option);

         void untrack_job(const std::shared_ptr<CompoundJob>& compound_job);
         void untrack_job(const std::string& hostname);

         bool is_a_job_running(const std::string& hostname);
         std::shared_ptr<RunningJob> get_running_job(const std::shared_ptr<CompoundJob>& compound_job);
         std::shared_ptr<RunningJob> get_running_job(const std::string& hostname);

         static std::shared_ptr<JobTracker> create_tracker(std::vector<std::string> hostnames) {
           return std::shared_ptr<JobTracker>(new JobTracker(std::move(hostnames)));
         }

        // Iterable interface
        auto begin() { return _running_jobs.begin(); }
        auto end()   { return _running_jobs.end(); }
        auto begin() const { return _running_jobs.begin(); }
        auto end()   const { return _running_jobs.end(); }

    private:
        JobTracker(std::vector<std::string> hostnames);

        std::map<std::string, std::shared_ptr<RunningJob>> _running_jobs;
        std::map<std::shared_ptr<CompoundJob>, std::shared_ptr<RunningJob>> _compound_job_to_running_job;
    };

    inline bool JobTracker::is_a_job_running(const std::string& hostname) {
        return (_running_jobs.find(hostname) != _running_jobs.end());
    }

};


#endif //RUNNINGJOBTRACKER_H
