#ifndef SCHEDULINGALGORITHMSTATICFORESIGHTED_H
#define SCHEDULINGALGORITHMSTATICFORESIGHTED_H

#include "SchedulingAlgorithmStatic.h"

namespace wrench {
    /**
     * Static foresighted heuristic (FS-x in Section V-D):
     * selects the best combination of options for all remaining tasks, according to the comparator.
     */
    class SchedulingAlgorithmStaticForesighted : public SchedulingAlgorithmStatic {
    public:
        SchedulingAlgorithmStaticForesighted(
            const std::shared_ptr<ApplicationSpecs> &application_specs,
            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double
                (double, double)> > > > &exec_options,
            ProbabilityComputation *probability_computation,
            OptionComparatorFunction *comparator_function)
            : SchedulingAlgorithmStatic(application_specs, "static_foresighted", exec_options,
                                        probability_computation, comparator_function) {
        }

        void preprocess_host_decisions(const std::string &hostname,
                                       double initial_data_size,
                                       double initial_error_level,
                                       double deadline,
                                       bool lower_bound) override;

        void reset_host_preprocessed_decisions(const std::string &hostname) override {
            SchedulingAlgorithmStatic::reset_host_preprocessed_decisions(hostname);
        }

        void reset_all_preprocessed_decisions() override {
            SchedulingAlgorithmStatic::reset_all_preprocessed_decisions();
        }
    };
}

#endif
