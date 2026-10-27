#ifndef HEURISTICPLANNING_H
#define HEURISTICPLANNING_H

#include <memory>
#include <string>
#include <vector>

#include "ApplicationSpecs.h"
#include "ProbabilityComputation.h"

/**
 * Calculations shared by the static (one PE, Section V-D) and greedy (N PEs, Section VI-A) heuristics.
 *
 * Both heuristics choose among candidates: options of the next task (nearsighted) or combinations of
 * options for all remaining tasks (foresighted), each with a probability of success and an error level.
 * The greedy heuristics allocate their N PEs to candidates with allocate_pes(); the static heuristics
 * are the degenerate case N = 1.
 */
namespace wrench {
    /// Criterion used to choose among candidates
    enum class Criterion {
        ExpectedError,      ///< X: lowest expected error
        ProbabilitySuccess, ///< P: highest probability of success
        SuccessErrorRatio,  ///< R: highest ratio of probability of success to error level
        ErrorLevel          ///< E: lowest error level, among candidates that can succeed
    };

    /// @throws std::invalid_argument for unknown criterion names
    Criterion parse_criterion(const std::string &name);

    /// An option of the next task, or a combination of options for all remaining tasks
    struct Candidate {
        std::vector<std::string> options;
        double probability_success;
        double error_level;
    };

    /**
     * @brief Nearsighted candidates: the options of the next task, assuming it is the last task in the chain,
     *        in decision tree order
     */
    std::vector<Candidate> nearsighted_candidates(const std::shared_ptr<ApplicationSpecs> &application_specs,
                                                  ProbabilityComputation *probability_computation,
                                                  const ApplicationSpecs::ExecOptionDecisionNode *node,
                                                  const std::string &task,
                                                  double input_data_size,
                                                  double input_error_level,
                                                  double remaining_time);

    /**
     * @brief Foresighted candidates: the combinations of options for all remaining tasks, by increasing error level
     */
    std::vector<Candidate> foresighted_candidates(const std::shared_ptr<ApplicationSpecs> &application_specs,
                                                  ProbabilityComputation *probability_computation,
                                                  const ApplicationSpecs::ExecOptionDecisionNode *node,
                                                  int task_index,
                                                  double input_data_size,
                                                  double input_error_level,
                                                  double deadline,
                                                  bool lower_bound);

    /**
     * @brief Expected error when num_pes[k] PEs each independently execute candidate k: the lowest error level
     *        among successful executions, or e_fail if all fail. Candidates must be sorted by increasing error
     *        level (unless at most one candidate is executed).
     */
    double expected_error(const std::vector<Candidate> &candidates, const std::vector<long> &num_pes, double e_fail);

    /**
     * @brief Number of PEs, out of num_pes, that execute each candidate. For criterion X, PEs are added one at
     *        a time to the candidate that leads to the lowest expected error; for the other criteria, all PEs
     *        execute the best candidate. Ties go to the earliest candidate.
     */
    std::vector<long> allocate_pes(Criterion criterion, const std::vector<Candidate> &candidates, long num_pes,
                                   double e_fail);

    /// @brief The candidate that a single PE executes, i.e., allocate_pes() with num_pes = 1
    const Candidate &best_candidate(Criterion criterion, const std::vector<Candidate> &candidates, double e_fail);

    /**
     * @brief All combinations of options (one per task) below a decision node
     */
    void collect_combinations(std::vector<std::vector<std::string> > &all_combinations,
                              const ApplicationSpecs::ExecOptionDecisionNode *node,
                              std::vector<std::string> &current_path);

    /**
     * @brief Error level of the output of a combination of options executed from a decision node
     */
    double calculate_error_level_one_host(const ApplicationSpecs::ExecOptionDecisionNode *current_node,
                                          const std::vector<std::string> &combo,
                                          int relative_task_index);
}

#endif //HEURISTICPLANNING_H
