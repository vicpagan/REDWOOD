#include <fstream>
#include <iostream>
#include <boost/json.hpp>
#include "Utils.h"

/**
 * @brief Helper function to read a JSON object from a file
 * @param filepath: the file path
 * @return a boost::json::object object
 */
boost::json::object readJSONFromFile(const std::string& filepath) {
    // Open the file using ifstream
    std::ifstream file(filepath);

    // Check if the file was opened successfully
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filepath << std::endl;
        exit(1);
    }

    // Read the whole file into a string
    std::string json_string((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
    file.close();

    // Parse string into an object
    auto json_object = boost::json::parse(json_string).as_object();
    return json_object;
}