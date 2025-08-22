#include "RunningJob.h"
#include "RunningJobTracker.h"

namespace wrench {
    void RunningJobTracker::track_job(const std::shared_ptr<CompoundJob>& compound_job,
                                      const std::string& hostname,
                                      const std::string& task_name,
                                      const std::string& execution_option) {
        auto running_job = std::shared_ptr<RunningJob>(new RunningJob(compound_job, task_name, execution_option));
        _compound_job_to_running_job[compound_job] = running_job;
        _running_jobs[hostname] = running_job;
    }

    void RunningJobTracker::untrack_job(const std::shared_ptr<CompoundJob>& compound_job) {
        std::string hostname = _compound_job_to_running_job.at(compound_job)->get_hostname();
        _running_jobs.erase(hostname);
        _compound_job_to_running_job.erase(compound_job);
    }

    void RunningJobTracker::untrack_job(const std::string& hostname) {
        auto compound_job = _running_jobs.at(hostname)->get_compound_job();
        _compound_job_to_running_job.erase(compound_job);
        _running_jobs.erase(hostname);
    }

    std::shared_ptr<RunningJob> RunningJobTracker::get_running_job(const std::shared_ptr<CompoundJob>& compound_job) {
        return _compound_job_to_running_job.at(compound_job);
    }

    std::shared_ptr<RunningJob> RunningJobTracker::get_running_job(const std::string& hostname) {
        if (_running_jobs.find(hostname) == _running_jobs.end()) {
            return nullptr;
        }
        else {
            return _running_jobs.at(hostname);
        }
    }
};
