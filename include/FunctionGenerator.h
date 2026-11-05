#ifndef FUNCTIONGENERATOR_H
#define FUNCTIONGENERATOR_H

#include <boost/json.hpp>
#include <map>


class FunctionGenerator {
public:
    FunctionGenerator() = default;
    static std::function<double(double, double)> get_function(const boost::json::object& spec);

    class AffineFunctor {
    public:
        /**
         * @brief Constructor: the function a + b * x + c * y
         * @param a The constant term
         * @param b The coefficient of x
         * @param c The coefficient of y
         */
        AffineFunctor(double a, double b, double c): _a(a), _b(b), _c(c) {
        }

        /**
         * @brief Evaluate the function
         * @param x The data size
         * @param y The error level
         * @return a + b * x + c * y
         */
        double operator()(double x, double y) const {
            return _a + _b * x + _c * y;
        }

    private:
        double _a, _b, _c;
    };

    class QuadraticFunctor {
    public:
        /**
         * @brief Constructor: the function a + b * x + c * y + d * x^2 + e * y^2
         * @param a The constant term
         * @param b The coefficient of x
         * @param c The coefficient of y
         * @param d The coefficient of x^2
         * @param e The coefficient of y^2
         */
        QuadraticFunctor(double a, double b, double c, double d, double e): _a(a), _b(b), _c(c), _d(d), _e(e) {
        }

        /**
         * @brief Evaluate the function
         * @param x The data size
         * @param y The error level
         * @return a + b * x + c * y + d * x^2 + e * y^2
         */
        double operator()(double x, double y) const {
            return _a + _b * x + _c * y + _d * x * x + _e * y * y;
        }

    private:
        double _a, _b, _c, _d, _e;
    };

private:
    static std::map<std::string, std::function<double(double, double)>> _function_registry;
};


#endif //FUNCTIONGENERATOR_H
