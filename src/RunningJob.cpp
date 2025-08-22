#include "RunningJob.h"

namespace wrench {

    std::map<std::string, std::shared_ptr<RunningJob>> RunningJob::_running_jobs;
    std::map<std::shared_ptr<CompoundJob>, std::shared_ptr<RunningJob>> RunningJob::_compound_job_to_running_job;

    void RunningJob::track_job(const std::shared_ptr<CompoundJob>& compound_job,
                               const std::string& hostname,
                               const std::string& task_name,
                               const std::string& execution_option) {
        // Not using make_shared to avoid the super annoying private constructor error
        auto running_job = std::shared_ptr<RunningJob>(new RunningJob(compound_job, task_name, execution_option));
        _compound_job_to_running_job[compound_job] = running_job;
        _running_jobs[hostname] = running_job;
    }

    void RunningJob::untrack_job(const std::shared_ptr<CompoundJob>& compound_job) {
        std::string hostname = _compound_job_to_running_job[compound_job]->get_hostname();
        _running_jobs.erase(hostname);
        _compound_job_to_running_job.erase(compound_job);
    }

    void RunningJob::untrack_job(const std::string& hostname) {
        auto compound_job = _running_jobs[hostname]->get_compound_job();
        _compound_job_to_running_job.erase(compound_job);
        _running_jobs.erase(hostname);
    }

    std::shared_ptr<RunningJob> RunningJob::get_running_job(const std::shared_ptr<CompoundJob>& compound_job) {
        return _compound_job_to_running_job[compound_job];
    }

    std::shared_ptr<RunningJob> RunningJob::get_running_job(const std::string& hostname) {
        return _running_jobs[hostname];
    }
};
