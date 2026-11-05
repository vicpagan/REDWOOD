#ifndef SCHEDULINGALGORITHMDYNAMIC_H
#define SCHEDULINGALGORITHMDYNAMIC_H

#include <map>
#include <string>
#include <functional>
#include <memory>

#include "Controller.h"
#include "Discretization.h"
#include "SchedulingAlgorithm.h"

namespace wrench {
    class SchedulingAlgorithmDynamic : public SchedulingAlgorithm {
    public:
        /**
         * @brief Constructor
         * @param application_specs The application specification
         * @param name The algorithm's name (dynamic_lower or dynamic_upper)
         * @param exec_options The option functions of each option of each task
         * @param discretization The discretization
         * @param lower_bound Whether the table of best options is computed for the lower bound (instead of the
         *        upper bound) (Section V-B)
         */
        explicit SchedulingAlgorithmDynamic(
            const std::shared_ptr<ApplicationSpecs> &application_specs,
            const std::string &name,
            const std::map<std::string, std::map<std::string, std::map<std::string, std::function<double(double, double)>>>>& exec_options,
            Discretization *discretization,
            const bool lower_bound) : SchedulingAlgorithm(application_specs,
                name,
                exec_options,
                discretization,
                lower_bound) {
        };

        std::vector<SchedulingDecision> make_decisions(
            SystemState *system_state_tracker,
            double remaining_time) override;

        void preprocess_host_decisions(
            const std::string& hostname,
            double initial_data_size,
            double initial_error_level,
            double deadline,
            bool lower_bound) override;

        /**
         * @brief Discard the tables of best options of all hosts
         */
        void reset_all_preprocessed_decisions() override {
            _preprocessed_decisions_by_host.clear();
        }

        /**
         * @brief Discard the table of best options of a host, so that it is computed again
         * @param hostname The host
         */
        void reset_host_preprocessed_decisions(const std::string& hostname) override {
            _preprocessed_decisions_by_host.erase(hostname);
        }

    private:

        void fill_host_preprocessing_table(
            const std::string& hostname,
            double input_data_size,
            double input_error_level,
            double remaining_time,
            bool lower_bound);

        std::map<std::string, std::map<const ApplicationSpecs::ExecOptionDecisionNode*, std::vector<std::pair<long, std::string>>>> _preprocessed_decisions_by_host;
    };
}

#endif //SCHEDULINGALGORITHMDYNAMIC_H
