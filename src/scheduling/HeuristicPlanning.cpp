#include "scheduling/HeuristicPlanning.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>

#include "DeltaTComputation.h"

namespace wrench {
    // Criterion E only considers candidates whose probability of success is above this threshold
    static constexpr double ERROR_LEVEL_PROBABILITY_SUCCESS_THRESHOLD = 0.0;

    Criterion parse_criterion(const std::string &name) {
        if (name == "expected_error") return Criterion::ExpectedError;
        if (name == "probability_success") return Criterion::ProbabilitySuccess;
        if (name == "success_error_ratio") return Criterion::SuccessErrorRatio;
        if (name == "error_level") return Criterion::ErrorLevel;
        throw std::invalid_argument("Unknown criterion '" + name + "'");
    }

    std::vector<Candidate> nearsighted_candidates(const std::shared_ptr<ApplicationSpecs> &application_specs,
                                                  ProbabilityComputation *probability_computation,
                                                  const ApplicationSpecs::ExecOptionDecisionNode *node,
                                                  const std::string &task,
                                                  const double input_data_size,
                                                  const double input_error_level,
                                                  const double remaining_time) {
        initialize_delta_t(application_specs, *probability_computation);

        std::vector<Candidate> candidates;
        for (const auto &child: node->children) {
            const auto &option_functions = application_specs->get_task_functions().at(task).at(child->execution_option);
            const double execution_time = application_specs->get_execution_time(option_functions, input_data_size,
                                                                                input_error_level);
            candidates.push_back({
                {child->execution_option},
                probability_computation->compute_probability(execution_time, remaining_time, false),
                option_functions.at("e_function")(input_data_size, input_error_level)
            });
        }
        return candidates;
    }

    std::vector<Candidate> foresighted_candidates(const std::shared_ptr<ApplicationSpecs> &application_specs,
                                                  ProbabilityComputation *probability_computation,
                                                  const ApplicationSpecs::ExecOptionDecisionNode *node,
                                                  const int task_index,
                                                  const double input_data_size,
                                                  const double input_error_level,
                                                  const double deadline,
                                                  const bool lower_bound) {
        initialize_delta_t(application_specs, *probability_computation);

        // All combinations, sorted by error level
        std::vector<std::vector<std::string> > all_combinations;
        std::vector<std::string> current_path;
        collect_combinations(all_combinations, node, current_path);

        std::map<std::vector<std::string>, double> el_by_combo;
        for (const auto &combo: all_combinations) {
            el_by_combo.emplace(combo, calculate_error_level_one_host(node, combo, 0));
        }
        std::sort(all_combinations.begin(), all_combinations.end(), [&](const auto &a, const auto &b) {
            return el_by_combo.at(a) < el_by_combo.at(b);
        });

        std::vector<Candidate> candidates;
        for (const auto &combo: all_combinations) {
            candidates.push_back({
                combo,
                probability_computation->compute_success_probability(*application_specs, task_index, combo,
                                                                     input_data_size, input_error_level, deadline,
                                                                     lower_bound),
                el_by_combo.at(combo)
            });
        }
        return candidates;
    }

    double expected_error(const std::vector<Candidate> &candidates, const std::vector<long> &num_pes,
                          const double e_fail) {
        double weighted_sum = 0.0;
        double p_all_fail = 1.0; // probability that all executions of the candidates so far fail
        for (size_t k = 0; k < candidates.size(); k++) {
            const double p_one_succeeds = 1.0 - std::pow(1.0 - candidates[k].probability_success, num_pes[k]);
            weighted_sum += candidates[k].error_level * p_one_succeeds * p_all_fail;
            p_all_fail *= (1.0 - p_one_succeeds);
        }
        return weighted_sum + e_fail * p_all_fail;
    }

    std::vector<long> allocate_pes(const Criterion criterion, const std::vector<Candidate> &candidates,
                                   const long num_pes, const double e_fail) {
        if (candidates.empty()) {
            throw std::runtime_error("allocate_pes(): no candidates");
        }
        std::vector<long> allocation(candidates.size(), 0);
        size_t best = 0;

        switch (criterion) {
            case Criterion::ExpectedError:
                for (long pe = 0; pe < num_pes; pe++) {
                    double best_expected_error = std::numeric_limits<double>::infinity();
                    size_t to_add = 0;
                    for (size_t k = 0; k < candidates.size(); k++) {
                        allocation[k]++;
                        const double value = expected_error(candidates, allocation, e_fail);
                        if (value < best_expected_error) {
                            best_expected_error = value;
                            to_add = k;
                        }
                        allocation[k]--;
                    }
                    allocation[to_add]++;
                }
                return allocation;

            case Criterion::ProbabilitySuccess: {
                double best_value = -std::numeric_limits<double>::infinity();
                for (size_t k = 0; k < candidates.size(); k++) {
                    if (candidates[k].probability_success > best_value) {
                        best_value = candidates[k].probability_success;
                        best = k;
                    }
                }
                break;
            }
            case Criterion::SuccessErrorRatio: {
                double best_value = -std::numeric_limits<double>::infinity();
                for (size_t k = 0; k < candidates.size(); k++) {
                    const double ratio = candidates[k].probability_success / candidates[k].error_level;
                    if (ratio > best_value) {
                        best_value = ratio;
                        best = k;
                    }
                }
                break;
            }
            case Criterion::ErrorLevel: {
                // If no candidate can succeed, the first one
                double best_value = std::numeric_limits<double>::infinity();
                for (size_t k = 0; k < candidates.size(); k++) {
                    if (candidates[k].probability_success > ERROR_LEVEL_PROBABILITY_SUCCESS_THRESHOLD &&
                        candidates[k].error_level < best_value) {
                        best_value = candidates[k].error_level;
                        best = k;
                    }
                }
                break;
            }
        }
        allocation[best] = num_pes;
        return allocation;
    }

    const Candidate &best_candidate(const Criterion criterion, const std::vector<Candidate> &candidates,
                                    const double e_fail) {
        const auto allocation = allocate_pes(criterion, candidates, 1, e_fail);
        return candidates[std::find(allocation.begin(), allocation.end(), 1) - allocation.begin()];
    }

    void collect_combinations(
        std::vector<std::vector<std::string> > &all_combinations,
        const ApplicationSpecs::ExecOptionDecisionNode *node,
        std::vector<std::string> &current_path) {
        if (node->is_leaf) {
            all_combinations.push_back(current_path);
            return;
        }

        for (const auto &child: node->children) {
            current_path.push_back(child->execution_option);
            collect_combinations(all_combinations, child.get(), current_path);
            current_path.pop_back();
        }
    }

    double calculate_error_level_one_host(
        const ApplicationSpecs::ExecOptionDecisionNode *current_node,
        const std::vector<std::string> &combo,
        int relative_task_index) {
        if (current_node->is_leaf) {
            return current_node->cumulative_error_factor;
        }

        for (const auto &child: current_node->children) {
            if (child->execution_option == combo[relative_task_index]) {
                return calculate_error_level_one_host(child.get(), combo, relative_task_index + 1);
            }
        }

        throw std::runtime_error("No matching child found for option: " + combo[relative_task_index]);
    }
}
