#ifndef HEURISTICPLANNING_H
#define HEURISTICPLANNING_H

#include <string>
#include <vector>

#include "ApplicationSpecs.h"
#include "Discretization.h"

/**
 * Calculations shared by the static (one host, Section V-D) and greedy (N hosts, Section VI-A) heuristics.
 *
 * A heuristic chooses among sequences of options: single options of the next task (nearsighted), or options
 * for all remaining tasks (foresighted). Each sequence has a probability of success (ps) and an error level (el),
 * kept in three parallel vectors. The greedy heuristics spread N hosts over the sequences with allocate_hosts();
 * the static heuristics (and every per-host decision) are the case N = 1, i.e. best_choice().
 *
 * Criteria (the end of the algorithm name):
 *   expected_error:      lowest expected error
 *   probability_success: highest probability of success
 *   success_error_ratio: highest ratio of probability of success to error level
 *   error_level:         lowest error level, among sequences that can succeed
 */
namespace wrench {
    bool is_valid_criterion(const std::string &criterion);

    void nearsighted_choices(const ApplicationSpecs &application_specs,
                             const Discretization &discretization,
                             const ApplicationSpecs::ExecOptionDecisionNode *node,
                             int task_index,
                             double input_data_size,
                             double input_error_level,
                             double remaining_time,
                             bool lower_bound,
                             std::vector<std::vector<std::string> > &sequences,
                             std::vector<double> &ps,
                             std::vector<double> &el);

    void foresighted_choices(const ApplicationSpecs &application_specs,
                             const Discretization &discretization,
                             const ApplicationSpecs::ExecOptionDecisionNode *node,
                             int task_index,
                             double input_data_size,
                             double input_error_level,
                             double remaining_time,
                             bool lower_bound,
                             std::vector<std::vector<std::string> > &sequences,
                             std::vector<double> &ps,
                             std::vector<double> &el);

    void sort_by_error_level(std::vector<std::vector<std::string> > &sequences,
                             std::vector<double> &ps,
                             std::vector<double> &el);

    double expected_error_on_hosts(const std::vector<double> &ps,
                                   const std::vector<double> &el,
                                   const std::vector<long> &num_hosts,
                                   double e_fail);

    std::vector<long> allocate_hosts(const std::string &criterion,
                                     const std::vector<double> &ps,
                                     const std::vector<double> &el,
                                     long num_hosts,
                                     double e_fail);

    size_t best_choice(const std::string &criterion,
                       const std::vector<double> &ps,
                       const std::vector<double> &el,
                       double e_fail);

    void collect_combinations(std::vector<std::vector<std::string> > &all_combinations,
                              const ApplicationSpecs::ExecOptionDecisionNode *node,
                              std::vector<std::string> &current_path);

    double calculate_error_level_one_host(const ApplicationSpecs::ExecOptionDecisionNode *current_node,
                                          const std::vector<std::string> &combo,
                                          int relative_task_index);
}

#endif //HEURISTICPLANNING_H
