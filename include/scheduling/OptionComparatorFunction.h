#ifndef REDWOOD_OPTIONCOMPARATORFUNCTION_H
#define REDWOOD_OPTIONCOMPARATORFUNCTION_H

#include <string>
#include <map>
#include <functional>
#include "ProbabilityComputation.h"

namespace wrench {
    class OptionComparatorFunction {
    public:

        explicit OptionComparatorFunction(const std::shared_ptr<ApplicationSpecs> &application_specs, std::string name)
            : _application_specs(application_specs), _name(std::move(name)) {}

        virtual ~OptionComparatorFunction() = default;

        virtual double comp_value(
            ProbabilityComputation *probability_computation,
            const std::map<std::string, std::function<double(double, double)> > &option_functions,
            double input_data_size,
            double input_error_level,
            double remaining_time
        ) const = 0;

        virtual bool is_minimizing() const = 0;
        
        static std::shared_ptr<OptionComparatorFunction> create_scheduling_algorithm(
            const std::string& type, std::shared_ptr<ApplicationSpecs> application_specs);

        std::string get_name() { return _name; }

    protected:
        std::shared_ptr<ApplicationSpecs> _application_specs;
        std::string _name;
    };

    class ExpectedErrorComparator : public OptionComparatorFunction {
    public:
        explicit ExpectedErrorComparator(const std::shared_ptr<ApplicationSpecs> &application_specs) : OptionComparatorFunction(application_specs, "expected_error"),
                                                                                           _io_read_bandwidth_per_node(application_specs->get_io_read_bandwidth_per_node()),
                                                                                           _io_write_bandwidth_per_node(application_specs->get_io_write_bandwidth_per_node()),
                                                                                           _e_fail(application_specs->get_e_fail()) {
        }

        double comp_value(
            ProbabilityComputation *probability_computation,
            const std::map<std::string, std::function<double(double, double)> > &option_functions,
            double input_data_size,
            double input_error_level,
            double remaining_time) const override;

        bool is_minimizing() const override { return true; }

    private:
        double _io_read_bandwidth_per_node;
        double _io_write_bandwidth_per_node;
        double _e_fail;
    };

    class ProbabilitySuccessComparator : public OptionComparatorFunction {
    public:
        explicit ProbabilitySuccessComparator(const std::shared_ptr<ApplicationSpecs> &application_specs) : OptionComparatorFunction(application_specs, "probability_success"),
                                                                                   _io_read_bandwidth_per_node(application_specs->get_io_read_bandwidth_per_node()),
                                                                                   _io_write_bandwidth_per_node(application_specs->get_io_write_bandwidth_per_node()) {
        }

        double comp_value(
            ProbabilityComputation *probability_computation,
            const std::map<std::string, std::function<double(double, double)> > &option_functions,
            double input_data_size,
            double input_error_level,
            double remaining_time) const override;

        bool is_minimizing() const override { return false; }


    private:
        double _io_read_bandwidth_per_node;
        double _io_write_bandwidth_per_node;
    };

    class ErrorLevelComparator : public OptionComparatorFunction {
    public:
        explicit ErrorLevelComparator(const std::shared_ptr<ApplicationSpecs> &application_specs) : OptionComparatorFunction(application_specs, "error_level"),
                                                                                   _io_read_bandwidth_per_node(application_specs->get_io_read_bandwidth_per_node()),
                                                                                   _io_write_bandwidth_per_node(application_specs->get_io_write_bandwidth_per_node()),
                                                                                   _prob_success_threshold(0.0) {
        }

        double comp_value(
            ProbabilityComputation *probability_computation,
            const std::map<std::string, std::function<double(double, double)> > &option_functions,
            double input_data_size,
            double input_error_level,
            double remaining_time) const override;

        bool is_minimizing() const override { return true; }

        double get_prob_success_threshold() const { return _prob_success_threshold; }

    private:
        double _io_read_bandwidth_per_node;
        double _io_write_bandwidth_per_node;
        double _prob_success_threshold;
    };

    class SuccessErrorRatioComparator : public OptionComparatorFunction {
    public:
        explicit SuccessErrorRatioComparator(const std::shared_ptr<ApplicationSpecs> &application_specs) : OptionComparatorFunction(application_specs, "success_error_ratio"),
                                                                                   _io_read_bandwidth_per_node(application_specs->get_io_read_bandwidth_per_node()),
                                                                                   _io_write_bandwidth_per_node(application_specs->get_io_write_bandwidth_per_node()) {
        }

        double comp_value(
            ProbabilityComputation *probability_computation,
            const std::map<std::string, std::function<double(double, double)> > &option_functions,
            double input_data_size,
            double input_error_level,
            double remaining_time) const override;

        bool is_minimizing() const override { return false; }


    private:
        double _io_read_bandwidth_per_node;
        double _io_write_bandwidth_per_node;
    };
}

#endif //REDWOOD_OPTIONCOMPARATORFUNCTION_H