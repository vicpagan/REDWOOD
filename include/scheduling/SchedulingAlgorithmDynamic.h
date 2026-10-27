#ifndef SCHEDULINGALGORITHMDYNAMIC_H
#define SCHEDULINGALGORITHMDYNAMIC_H

#include <map>
#include <string>
#include <functional>
#include <memory>

#include "Controller.h"
#include "ProbabilityComputation.h"
#include "SchedulingAlgorithm.h"

namespace wrench {
    class SchedulingAlgorithmDynamic : public SchedulingAlgorithm {
    public:
        /**
         * @param lower_bound Whether the decision table is computed for the lower bound (optimistic) on the
         *        expected error, instead of the upper bound (pessimistic) (Section V-B)
         */
        explicit SchedulingAlgorithmDynamic(
            const std::shared_ptr<ApplicationSpecs> &application_specs,
            const std::string &name,
            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double
                (double, double)> > > > &exec_options,
            ProbabilityComputation *probability_computation,
            const bool lower_bound) : SchedulingAlgorithm(application_specs,
                                                          name,
                                                          exec_options,
                                                          probability_computation),
                                      _lower_bound(lower_bound) {
        };

        std::vector<SchedulingDecision> make_decisions(
            SystemState *system_state_tracker,
            double remaining_time) override;

        void preprocess_host_decisions(
            const std::string &hostname,
            double initial_data_size,
            double initial_error_level,
            double deadline,
            bool lower_bound) override;

        void reset_all_preprocessed_decisions() override {
            _preprocessed_decisions_by_host.clear();
        }

        void reset_host_preprocessed_decisions(const std::string &hostname) override {
            _preprocessed_decisions_by_host.erase(hostname);
        }

    private:
        bool _lower_bound;
        std::map<std::string, std::map<const ApplicationSpecs::ExecOptionDecisionNode *, std::vector<std::pair<long,
            std::string> > > > _preprocessed_decisions_by_host;
    };
}

#endif //SCHEDULINGALGORITHMDYNAMIC_H
