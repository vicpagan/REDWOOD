#include <cmath>
#include <iostream>
#include <chrono>

double lambda = 0.0001;
double epsilon = 1e-6;

// Relative closeness check with a small epsilon
bool is_equal(double a, double b) {
    return fabs(a - b) < epsilon * std::max(fabs(a), fabs(b));
}

double success_probability(int u, double delta_t) {
    return exp(-u * lambda * delta_t);
}

// This is p_u
double fail_probability(int u, double delta_t) {
    return success_probability(u, delta_t) - success_probability(u + 1, delta_t);
}

// Bottom up recursive function P(m, n)
double probability(double *dp, int m, int n, double delta_t) {

    for (int i = m; i <= n; ++i) {
        double running_probability = success_probability(m, delta_t);
        for (int k = 0; k < m; ++k) {
            running_probability += fail_probability(k, delta_t) * dp[i - k - 1];
        }
        dp[i] = running_probability;
    }
    return dp[n];
}

int main (int argc, char *argv[]) {

    if (argc != 4) {
        printf("Usage: %s <t> <d> <delta multiplier> \n", argv[0]);
        return 1;
    }

    double t = atof(argv[1]);
    double d = atof(argv[2]);
    double delta_t = 0.5;
    double mult_delta_t = atof(argv[3]);
    double test_delta_t = delta_t * mult_delta_t;


    int repeats = 25;

    // Tests with set delta_t
    // This is meant to be the baseline for comparison
    int m = static_cast<int>(ceil(t / delta_t));
    int n = static_cast<int>(ceil(d / delta_t));

    std::chrono::duration<double, std::milli> duration = std::chrono::duration<double, std::milli>::zero();
    double result = 0.0;
    for (int r = 0; r < repeats; ++r) {
        auto *dp = new double[n + 1];
        for (int i = 0; i <= n; ++i) {
            dp[i] = 0.0;
        }

        auto start = std::chrono::high_resolution_clock::now();

        result += probability(dp, m, n, delta_t);

        auto end = std::chrono::high_resolution_clock::now();

        duration += end - start;
        delete[] dp;
    }
    result /= repeats;

    // Tests with a multiplier for delta_t
    int m_2 = static_cast<int>(ceil(t / test_delta_t));
    int n_2 = static_cast<int>(ceil(d / test_delta_t));

    std::chrono::duration<double, std::milli> duration_2 = std::chrono::duration<double, std::milli>::zero();
    double result_2 = 0.0;
    for (int r = 0; r < repeats; ++r) {
        auto *dp = new double[n_2 + 1];
        for (int i = 0; i <= n_2; ++i) {
            dp[i] = 0.0;
        }

        auto start = std::chrono::high_resolution_clock::now();

        result_2 += probability(dp, m_2, n_2, test_delta_t);

        auto end = std::chrono::high_resolution_clock::now();

        duration_2 += end - start;
        delete[] dp;
    }
    result_2 /= repeats;

    std::cout << "Lambda: " << lambda << std::endl;
    std::cout << "t: " << t << ", d: " << d << std::endl;
    std::cout << "m: " << m << ", n: " << n << std::endl;
    std::cout << "delta_t: " << delta_t << std::endl;
    std::cout << "Probability: " << result << std::endl;
    std::cout << "Probability (" << mult_delta_t << "x delta): " << result_2 << std::endl;
    std::cout << "Accurate: " << (is_equal(result, result_2) ? "Yes" : "No") << " (within " << epsilon << ")" << std::endl;
    std::cout << "Avg Exec Time: " << (duration.count() / repeats) << " ms" << std::endl;
    std::cout << "Avg Exec Time (" << mult_delta_t << "x delta): " << (duration_2.count() / repeats) << " ms" << std::endl;
    std::cout << "Speedup: " << (duration.count() / duration_2.count()) << "x" << std::endl;
    return 0;
}