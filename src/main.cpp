#include <cmath>
#include <vector>
#include <iostream>

#include "ProbabilityComputation.h"

int main (int argc, char *argv[]) {

    if (argc != 5) {
        printf("Usage: %s <lambda> <restart overhead> <task time> <time to deadline>\n", argv[0]);
        return 1;
    }

    const double lambda = atof(argv[1]);
    const double restart_overhead = atof(argv[2]);
    const double task_time = atof(argv[3]);
    const double time_to_deadline = atof(argv[4]);

    // Allocate object
    auto prob = new ProbabilityComputation(lambda, restart_overhead);

    // Compute the best delta_t with precision 0.01 (hardcoded)
    double best_delta_t = prob->compute_best_deltat(task_time, time_to_deadline, 1e-2);
    std::cout << "Best delta_t: " << best_delta_t << std::endl;

    // See what happens for worse delta_t
    for (int factor = 5; factor < 20; factor += 1) {
        double candidate_delta_t = best_delta_t * (factor * 0.1);

        prob->set_delta_t(candidate_delta_t);

        double probability_upper_bound = prob->compute_probability(task_time, time_to_deadline, false);
        double probability_lower_bound = prob->compute_probability(task_time, time_to_deadline, true);

        double probability_midpoint = (probability_upper_bound + probability_lower_bound) / 2;

        if (factor == 10) {
            std::cout << "* ";
        } else {
            std::cout << "  ";
        }
        std::cout << "With delta_t: " << candidate_delta_t << "   Prob = " << probability_midpoint << std::endl;
    }

    return 0;
}
