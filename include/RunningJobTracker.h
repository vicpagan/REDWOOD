#ifndef RUNNINGJOBTRACKER_H
#define RUNNINGJOBTRACKER_H

#include <map>
#include <string>

namespace wrench {

    class CompoundJob;
    class RunningJob;
    
    class RunningJobTracker {
      
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

         void clear_all();

         static std::shared_ptr<RunningJobTracker> create_tracker() {
           return std::shared_ptr<RunningJobTracker>(new RunningJobTracker());
         }

    private:
        RunningJobTracker() = default;

        std::map<std::string, std::shared_ptr<RunningJob>> _running_jobs;
        std::map<std::shared_ptr<CompoundJob>, std::shared_ptr<RunningJob>> _compound_job_to_running_job;
    };

    inline bool RunningJobTracker::is_a_job_running(const std::string& hostname) {
        return (_running_jobs.find(hostname) != _running_jobs.end());
    }

};


#endif //RUNNINGJOBTRACKER_H
