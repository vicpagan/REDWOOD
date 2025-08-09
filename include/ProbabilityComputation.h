#ifndef PROBABILITY_H
#define PROBABILITY_H

class ProbabilityComputation  {

public:

    explicit ProbabilityComputation(const double lambda) {
        this->lambda = lambda;
        this->delta_t = -1.0;
    }

    [[nodiscard]] double compute_probability(double task_time, double time_to_deadline) const;

    double compute_best_deltat(double task_time, double time_to_deadline, double precision);

    void set_delta_t(const double delta_t) {
        this->delta_t = delta_t;
    }

    [[nodiscard]] double get_delta_t() const {
        return this->delta_t;
    }

private:
    bool is_equal(double a, double b);
    [[nodiscard]] double success_probability(long u) const;
    [[nodiscard]] double fail_probability(long u) const;
    double compute_probability(std::vector<double>& dp, long m, long n) const;

    double lambda;
    double delta_t;
};

#endif

