#include <fstream>
#include <iostream>
#include <boost/json.hpp>
#include <cmath>
#include "Utils.h"

/**
 * @brief Helper function to read a JSON object from a file (exits the program if the file cannot be opened)
 * @param filepath The file path
 * @return The parsed JSON object
 */
boost::json::object readJSONFromFile(const std::string& filepath) {
    // open the file using ifstream
    std::ifstream file(filepath);

    // check if the file was opened successfully
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filepath << std::endl;
        exit(1);
    }

    // read the whole file into a string
    std::string json_string((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
    file.close();

    // parse string into an object
    auto json_object = boost::json::parse(json_string).as_object();
    return json_object;
}
