#ifndef SCHEDULINGALGORITHMGREEDYFORESIGHTED_H
#define SCHEDULINGALGORITHMGREEDYFORESIGHTED_H

#include "SchedulingAlgorithmGreedy.h"

namespace wrench {
    /**
     * Greedy foresighted heuristic (Section VI-A): at time zero, the number of PEs that execute each
     * combination of options is determined greedily; afterwards, PEs plan like the static foresighted heuristic.
     */
    class SchedulingAlgorithmGreedyForesighted : public SchedulingAlgorithmGreedy {
    public:
        SchedulingAlgorithmGreedyForesighted(
            const std::shared_ptr<ApplicationSpecs> &application_specs,
            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double
                (double, double)> > > > &exec_options,
            ProbabilityComputation *probability_computation,
            const Criterion criterion)
            : SchedulingAlgorithmGreedy(application_specs, "greedy_foresighted", exec_options,
                                        probability_computation, criterion) {
        }

        void initial_decisions(const std::string &hostname,
                               double initial_data_size,
                               double initial_error_level,
                               double deadline,
                               bool lower_bound) override;

        void preprocess_host_decisions(const std::string &hostname,
                                       double initial_data_size,
                                       double initial_error_level,
                                       double deadline,
                                       bool lower_bound) override;

        // FIXME: Change combinations storing to be reused for every scheduling calculation
        void reset_host_preprocessed_decisions(const std::string &hostname) override {
            SchedulingAlgorithmGreedy::reset_host_preprocessed_decisions(hostname);
            _nodes_per_combo_decision.clear();
        }

        void reset_all_preprocessed_decisions() override {
            SchedulingAlgorithmGreedy::reset_all_preprocessed_decisions();
            _nodes_per_combo_decision.clear();
        }

    protected:
        std::map<std::vector<std::string>, long> _nodes_per_combo_decision;

        void translate_to_static_decisions(SystemState *system_state_tracker) override;
    };
}

#endif
