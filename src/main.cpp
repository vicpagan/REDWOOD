#include <cmath>
#include <vector>
#include <iostream>
#include <chrono>

#include "ProbabilityComputation.h"

// Constants
// Default delta_t can be adjusted, but shouldn't we pick a small enough
// value to compare our choice of delta to for accuracy?
double default_delta_t = 0.5;
int repeats = 25;


int main (int argc, char *argv[]) {

    if (argc != 5) {
        printf("Usage: %s <lambda> <task time> <time to deadline> <restart overhead>\n", argv[0]);
        return 1;
    }

    double lambda = atof(argv[1]);
    double task_time = atof(argv[2]);
    double time_to_deadline = atof(argv[3]);
    double restart_overhead = atof(argv[4]);

    // Allocate object
    auto prob = new ProbabilityComputation(lambda, restart_overhead);

    std::chrono::duration<double, std::milli> duration = std::chrono::duration<double, std::milli>::zero();

    // Compute the best delta_t with precision 0.01 (hardcoded)
    auto start = std::chrono::high_resolution_clock::now();
    double best_delta_t = prob->compute_best_deltat(task_time, time_to_deadline, 0.01);
    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    std::cout << "Best delta_t: " << best_delta_t << std::endl;

    // See what happens for worse delta_t
    for (int factor = 5; factor < 20; factor += 1) {
        double candidate_delta_t = best_delta_t * (factor * 0.1);
        prob->set_delta_t(candidate_delta_t);
        double probability = prob->compute_probability(task_time, time_to_deadline);
        if (factor == 10) {
            std::cout << "* ";
        } else {
            std::cout << "  ";
        }
        std::cout << "With delta_t: " << candidate_delta_t << "   Prob = " << probability << std::endl;
    }

    return 0;
}
