#include "RunningJob.h"
#include "JobTracker.h"

#include <iostream>

namespace wrench {
    void JobTracker::track_job(const std::shared_ptr<CompoundJob>& compound_job,
                               const std::string& hostname,
                               const std::string& task_name,
                               const std::string& execution_option) {
        auto running_job = std::shared_ptr<RunningJob>(new RunningJob(compound_job, task_name, execution_option));
        _compound_job_to_running_job[compound_job] = running_job;
        _running_jobs[hostname] = running_job;
    }

    void JobTracker::untrack_job(const std::shared_ptr<CompoundJob>& compound_job) {
        std::string hostname = _compound_job_to_running_job.at(compound_job)->get_hostname();
        _running_jobs[hostname] = nullptr;
        _compound_job_to_running_job.erase(compound_job);
    }

    void JobTracker::untrack_job(const std::string& hostname) {
        auto compound_job = _running_jobs.at(hostname)->get_compound_job();
        _compound_job_to_running_job.erase(compound_job);
        _running_jobs[hostname] = nullptr;
    }

    std::shared_ptr<RunningJob> JobTracker::get_running_job(const std::shared_ptr<CompoundJob>& compound_job) {
        return _compound_job_to_running_job.at(compound_job);
    }

    std::shared_ptr<RunningJob> JobTracker::get_running_job(const std::string& hostname) {
        return _running_jobs.at(hostname);
    }

    JobTracker::JobTracker(const std::vector<std::string>& hostnames) {
        for (const auto& hostname : hostnames) {
            _running_jobs[hostname] = nullptr;
        }
    }
};
