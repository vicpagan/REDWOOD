#ifndef RUNNINGJOB_H
#define RUNNINGJOB_H

#include <map>
#include <string>

namespace wrench {
    class CompoundJob;

    class RunningJob {
    public:
        [[nodiscard]] std::string get_hostname() const { return _hostname; }
        [[nodiscard]] std::string get_task_name() const { return _task_name; }
        [[nodiscard]] std::string get_task_execution_option() const { return _task_execution_option; }
        [[nodiscard]] std::shared_ptr<CompoundJob> get_compound_job() const { return _compound_job; }

        static void track_job(
            const std::shared_ptr<CompoundJob>& compound_job,
            const std::string& hostname,
            const std::string& task_name,
            const std::string& execution_option);

        static void untrack_job(const std::shared_ptr<CompoundJob>& compound_job);
        static void untrack_job(const std::string& hostname);

        static std::shared_ptr<RunningJob> get_running_job(const std::shared_ptr<CompoundJob>& compound_job);
        static std::shared_ptr<RunningJob> get_running_job(const std::string& hostname);

        static void clear_all();

    private:
        RunningJob(const std::shared_ptr<CompoundJob>& compound_job,
                   std::string task_name,
                   std::string task_execution_option) : _compound_job(compound_job),
                                                        _task_name(std::move(task_name)),
                                                        _task_execution_option(std::move(task_execution_option)) {
        }

        std::string _hostname;
        std::shared_ptr<CompoundJob> _compound_job;
        std::string _task_name;
        std::string _task_execution_option;

        static std::map<std::string, std::shared_ptr<RunningJob>> _running_jobs;
        static std::map<std::shared_ptr<CompoundJob>, std::shared_ptr<RunningJob>> _compound_job_to_running_job;
    };

    inline void RunningJob::clear_all() {
        _running_jobs.clear();
        _compound_job_to_running_job.clear();
    }
    
};


#endif //RUNNINGJOB_H
