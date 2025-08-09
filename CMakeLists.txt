cmake_minimum_required(VERSION 3.12)
project(REDWOOD)

set(CMAKE_CXX_STANDARD 23)
include_directories(include)

add_executable(redwood src/main.cpp src/ProbabilityComputation.cpp include/ProbabilityComputation.h)
