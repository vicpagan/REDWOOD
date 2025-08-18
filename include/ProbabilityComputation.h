#ifndef PROBABILITY_H
#define PROBABILITY_H

class ProbabilityComputation  {

public:

    explicit ProbabilityComputation(const double lambda, const double restart_overhead) {
        this->lambda = lambda;
        this->restart_overhead = restart_overhead;
        this->delta_t = -1.0;
    }

    [[nodiscard]] double compute_probability(double task_time, double time_to_deadline, bool lower_bound) const;

    double compute_best_deltat(double task_time, double time_to_deadline, double precision);

    void set_delta_t(const double delta_t) {
        this->delta_t = delta_t;
    }

    [[nodiscard]] double get_delta_t() const {
        return this->delta_t;
    }

private:
    [[nodiscard]] double success_probability(long u) const;
    [[nodiscard]] double fail_probability(long u) const;
    double compute_probability(std::vector<double>& dp, long m, long n, long R) const;

    double lambda;
    double restart_overhead;
    double delta_t;
};

#endif

