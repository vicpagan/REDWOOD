#ifndef RUNNINGJOB_H
#define RUNNINGJOB_H

#include <map>
#include <string>
#include <memory>

namespace wrench {
    class CompoundJob;

    class RunningJob {
    public:
        [[nodiscard]] std::string get_hostname() const { return _hostname; }
        [[nodiscard]] std::string get_task_name() const { return _task_name; }
        [[nodiscard]] std::string get_task_execution_option() const { return _task_execution_option; }
        [[nodiscard]] std::shared_ptr<CompoundJob> get_compound_job() const { return _compound_job; }

    private:
        friend class RunningJobTracker;
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

    };

};


#endif //RUNNINGJOB_H
