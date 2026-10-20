#ifndef SCHEDULINGALGORITHMSTATICNEARSIGHTED_H
#define SCHEDULINGALGORITHMSTATICNEARSIGHTED_H

#include "SchedulingAlgorithmStatic.h"

namespace wrench {
    /**
     * Static nearsighted heuristic (NS-x in Section V-D):
     * selects the best option for the next task according to the comparator, as if it were the last task.
     */
    class SchedulingAlgorithmStaticNearsighted : public SchedulingAlgorithmStatic {
    public:
        SchedulingAlgorithmStaticNearsighted(
            const std::shared_ptr<ApplicationSpecs> &application_specs,
            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double
                (double, double)> > > > &exec_options,
            ProbabilityComputation *probability_computation,
            OptionComparatorFunction *comparator_function)
            : SchedulingAlgorithmStatic(application_specs, "static_nearsighted", exec_options,
                                        probability_computation, comparator_function) {
        }

        void preprocess_host_decisions(const std::string &hostname,
                                       double initial_data_size,
                                       double initial_error_level,
                                       double deadline,
                                       bool lower_bound) override;
    };
}

#endif
