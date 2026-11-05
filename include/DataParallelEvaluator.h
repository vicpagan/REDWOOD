/**
 * Copyright (c) 2017-2018. The WRENCH Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */


#ifndef DATA_PARALLEL_EVALUATOR_H
#define DATA_PARALLEL_EVALUATOR_H

#include <wrench-dev.h>
#include <boost/json/object.hpp>

#include "ApplicationSpecs.h"


namespace wrench {

    /**
     *  @brief Computes the optimal expected error when the tasks run data-parallel on N compute nodes,
     *         for several values of N
     */
    class DataParallelEvaluator  {
    public:
        // constructor
        DataParallelEvaluator(boost::json::object json_input, std::vector<unsigned long>& list_of_num_nodes);

        std::vector<std::pair<unsigned long, double>> evaluate();

    protected:

    private:
        unsigned long determine_max_num_compute_nodes();
        double compute_expected_error(unsigned long num_compute_nodes);

        boost::json::object json_input;
        std::vector<unsigned long> list_of_num_nodes;

    };
} // namespace wrench
#endif//DATA_PARALLEL_EVALUATOR_H
