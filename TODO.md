  - [VP] Implement the "pick the next execution option" based on recurcive expected error computation

  - [HC] Implement the functions ("affine", etc.) in a convenient data structure in src/FunctionDefinitions.cpp
        parse json to find: function type and parameter values
            std::function<double, double> global_functionmap.add_function(json_spec (defines "a", "b", "c")
            std::map<boost::json_object, std::function<double>>
            FunctionGenerator class

  - [HC] Modify the simulator to use a scheduling algorithm, loop over algorithms, and reset the RNG

