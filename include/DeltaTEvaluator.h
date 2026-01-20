/**
* Copyright (c) 2017-2018. The WRENCH Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */


#ifndef DELTA_T_EVALUATOR_H
#define DELTA_T_EVALUATOR_H

#include <wrench-dev.h>
#include <boost/json/object.hpp>

#include "ProbabilityComputation.h"
#include "ApplicationSpecs.h"
#include "scheduling/SchedulingAlgorithm.h"
#include "scheduling/OptionComparatorFunction.h"


namespace wrench {

    struct DeltaTResult {
        double delta_t;
        double upper_bound_error;
        double lower_bound_error;
        long long computation_time_ms;
    };

    /**
     *  @brief A DataParallelEvaluator implementation
     */
    class DeltaTEvaluator  {

    public:
        // Constructor
        explicit DeltaTEvaluator(boost::json::object json_input);

        std::vector<DeltaTResult> evaluate(double min_delta_t, double max_delta_t, double step_size);

    private:
        boost::json::object json_input;

    };

} // namespace wrench
#endif //DELTA_T_EVALUATOR_H
