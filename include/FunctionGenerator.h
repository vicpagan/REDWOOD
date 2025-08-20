#ifndef FUNCTIONGENERATOR_H
#define FUNCTIONGENERATOR_H

#include <boost/json.hpp>


class FunctionGenerator {
public:
    FunctionGenerator() = default;
    static std::function<double(double, double)> get_function(boost::json::object& spec);

    class AffineFunctor {
    public:
        AffineFunctor(double a, double b, double c): _a(a), _b(b), _c(c) {
        }

        double operator()(double x, double y) const {
            return _a + _b * x + _c * y;
        }

    private:
        double _a, _b, _c;
    };

    class QuadraticFunctor {
    public:
        QuadraticFunctor(double a, double b, double c, double d, double e): _a(a), _b(b), _c(c), _d(d), _e(e) {
        }

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
