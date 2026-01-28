cmake_minimum_required(VERSION 3.12)
message(STATUS "Cmake version ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}.${CMAKE_PATCH_VERSION}")

project(REDWOOD CXX)

# Disable annoying warnings
add_definitions("-DBOOST_ALLOW_DEPRECATED_HEADERS")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/CMakeModules/")

# Find Boost
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()
find_package(Boost REQUIRED)
find_package(Boost COMPONENTS json program_options REQUIRED)


# Find SimGrid
if (DEFINED SimGrid_PATH)
    if (NOT EXISTS "${SimGrid_PATH}")
        message(FATAL_ERROR "The specified SimGrid_PATH doesn't exit")
    else()
        file(GLOB LIBSIMGRID_FILES
                "${SimGrid_PATH}/lib/libsimgrid.so"
                "${SimGrid_PATH}/lib/libsimgrid.dylib"
        )
        if (NOT LIBSIMGRID_FILES)
            message(FATAL_ERROR "The specified SimGrid_PATH doesn't seem to contain a valid SimGrid installation")
        else()
            set(CMAKE_PREFIX_PATH ${SimGrid_PATH} ${CMAKE_PREFIX_PATH})
        endif()
    endif()
endif()
find_package(SimGrid REQUIRED)

# Find SimGrid's FS module
if (DEFINED FSMOD_PATH)
    if (NOT EXISTS "${FSMOD_PATH}")
        message(FATAL_ERROR "The specified FSMOD_PATH doesn't exit")
    else()
        file(GLOB LIBFSMOD_FILES
                "${FSMOD_PATH}/liblibfsmod.so"
                "${FSMOD_PATH}/lib/libfsmod.dylib"
        )
        if (NOT LIBFSMOD_FILES)
            message(FATAL_ERROR "The specified FSMOD_PATH doesn't seem to contain a valid FSMod installation")
        else()
            set(CMAKE_PREFIX_PATH ${FSMOD_PATH} ${CMAKE_PREFIX_PATH})
        endif()
    endif()
endif()
find_package(FSMod REQUIRED)

# Find WRENCH
if (DEFINED WRENCH_PATH)
    if (NOT EXISTS "${WRENCH_PATH}")
        message(FATAL_ERROR "The specified WRENCH_PATH doesn't exit")
    else()
        file(GLOB LIBWRENCH_FILES
                "${WRENCH_PATH}/lib/libwrench.a"
        )
        if (NOT LIBWRENCH_FILES)
            message(FATAL_ERROR "The specified WRENCH_PATH doesn't seem to contain a valid WRENCH installation")
        else()
            set(CMAKE_PREFIX_PATH ${WRENCH_PATH} ${CMAKE_PREFIX_PATH})
        endif()
    endif()
endif()
find_package(WRENCH REQUIRED)

# include directories
include_directories(include/ /usr/local/include/ /opt/local/include/ ${WRENCH_INCLUDE_DIR} ${SimGrid_INCLUDE_DIR} ${FSMOD_INCLUDE_DIR} ${Boost_INCLUDE_DIR})

# Simulator executable
set(SIMULATOR_SOURCE_FILES
        src/PlatformCreator.cpp
        src/NodeKiller.cpp
        src/Simulator.cpp
        src/Controller.cpp
        src/ApplicationSpecs.cpp
        src/ProbabilityComputation.cpp
        src/FunctionGenerator.cpp
        src/scheduling/OptionComparatorFunction.cpp
        src/scheduling/SchedulingAlgorithm.cpp
        src/scheduling/SchedulingAlgorithmDynamic.cpp
        src/scheduling/SchedulingAlgorithmStatic.cpp
        src/SystemState.cpp
        src/Utils.cpp
        include/PlatformCreator.h
        include/NodeKiller.h
        include/Controller.h
        include/ApplicationSpecs.h
        include/ProbabilityComputation.h
        include/FunctionGenerator.h
        include/scheduling/SchedulingAlgorithm.h
        include/scheduling/SchedulingAlgorithmDynamic.h
        include/scheduling/SchedulingAlgorithmStatic.h
        include/SystemState.h
        include/Utils.h
)

# Default simulator
add_executable(redwood_sim ${SIMULATOR_SOURCE_FILES})
target_link_libraries(redwood_sim
        ${WRENCH_LIBRARY}
        ${SimGrid_LIBRARY}
        ${FSMOD_LIBRARY}
        ${Boost_LIBRARIES}
        ${WRENCH_WFCOMMONS_WORKFLOW_PARSER_LIBRARY}
)

# Optimistic execution variant
add_executable(redwood_sim_opt_exec ${SIMULATOR_SOURCE_FILES})
target_compile_definitions(redwood_sim_opt_exec PRIVATE OPTIMISTIC_EXECUTION)
target_link_libraries(redwood_sim_opt_exec
        ${WRENCH_LIBRARY}
        ${SimGrid_LIBRARY}
        ${FSMOD_LIBRARY}
        ${Boost_LIBRARIES}
        ${WRENCH_WFCOMMONS_WORKFLOW_PARSER_LIBRARY}
)

# Optimistic scheduling variant
add_executable(redwood_sim_opt_sched ${SIMULATOR_SOURCE_FILES})
target_compile_definitions(redwood_sim_opt_sched PRIVATE OPTIMISTIC_DISCRETIZATION)
target_link_libraries(redwood_sim_opt_sched
        ${WRENCH_LIBRARY}
        ${SimGrid_LIBRARY}
        ${FSMOD_LIBRARY}
        ${Boost_LIBRARIES}
        ${WRENCH_WFCOMMONS_WORKFLOW_PARSER_LIBRARY}
)

# Both optimistic flags
add_executable(redwood_sim_opt_both ${SIMULATOR_SOURCE_FILES})
target_compile_definitions(redwood_sim_opt_both PRIVATE OPTIMISTIC_EXECUTION OPTIMISTIC_DISCRETIZATION)
target_link_libraries(redwood_sim_opt_both
        ${WRENCH_LIBRARY}
        ${SimGrid_LIBRARY}
        ${FSMOD_LIBRARY}
        ${Boost_LIBRARIES}
        ${WRENCH_WFCOMMONS_WORKFLOW_PARSER_LIBRARY}
)

set(DATA_PARALLEL_EVALUATOR_SOURCE_FILES
        src/DataParallelEvaluator.cpp
        src/ApplicationSpecs.cpp
        src/Utils.cpp
        src/scheduling/SchedulingAlgorithm.cpp
        src/scheduling/SchedulingAlgorithmDynamic.cpp
        src/scheduling/SchedulingAlgorithmStatic.cpp
        src/SystemState.cpp
        src/FunctionGenerator.cpp
        src/ProbabilityComputation.cpp
        include/ApplicationSpecs.h
        include/Utils.h
        include/scheduling/SchedulingAlgorithm.h
        include/scheduling/SchedulingAlgorithmDynamic.h
        include/scheduling/SchedulingAlgorithmStatic.h
        include/SystemState.h
        include/FunctionGenerator.h
        include/ProbabilityComputation.h
)

add_executable(data_parallel_evaluator ${DATA_PARALLEL_EVALUATOR_SOURCE_FILES})

target_link_libraries(data_parallel_evaluator
        ${WRENCH_LIBRARY}   # For UnitParser :)
        ${Boost_LIBRARIES}
        ${WRENCH_WFCOMMONS_WORKFLOW_PARSER_LIBRARY}
)

set(DELTA_T_EVALUATOR_SOURCE_FILES
        src/DeltatEvaluator.cpp
        src/ApplicationSpecs.cpp
        src/Utils.cpp
        src/scheduling/SchedulingAlgorithm.cpp
        src/scheduling/SchedulingAlgorithmDynamic.cpp
        src/scheduling/SchedulingAlgorithmStatic.cpp
        src/SystemState.cpp
        src/FunctionGenerator.cpp
        src/ProbabilityComputation.cpp
        include/ApplicationSpecs.h
        include/Utils.h
        include/scheduling/SchedulingAlgorithm.h
        include/scheduling/SchedulingAlgorithmDynamic.h
        include/scheduling/SchedulingAlgorithmStatic.h
        include/SystemState.h
        include/FunctionGenerator.h
        include/ProbabilityComputation.h
)

# Default delta_t_evaluator
add_executable(delta_t_evaluator ${DELTA_T_EVALUATOR_SOURCE_FILES})
target_link_libraries(delta_t_evaluator
        ${WRENCH_LIBRARY}   # For UnitParser :)
        ${Boost_LIBRARIES}
        ${WRENCH_WFCOMMONS_WORKFLOW_PARSER_LIBRARY}
)

# Optimistic execution variant
add_executable(delta_t_evaluator_opt_exec ${DELTA_T_EVALUATOR_SOURCE_FILES})
target_compile_definitions(delta_t_evaluator_opt_exec PRIVATE OPTIMISTIC_EXECUTION)
target_link_libraries(delta_t_evaluator_opt_exec
        ${WRENCH_LIBRARY}
        ${Boost_LIBRARIES}
        ${WRENCH_WFCOMMONS_WORKFLOW_PARSER_LIBRARY}
)

# Optimistic scheduling variant
add_executable(delta_t_evaluator_opt_sched ${DELTA_T_EVALUATOR_SOURCE_FILES})
target_compile_definitions(delta_t_evaluator_opt_sched PRIVATE OPTIMISTIC_DISCRETIZATION)
target_link_libraries(delta_t_evaluator_opt_sched
        ${WRENCH_LIBRARY}
        ${Boost_LIBRARIES}
        ${WRENCH_WFCOMMONS_WORKFLOW_PARSER_LIBRARY}
)

# Both optimistic flags
add_executable(delta_t_evaluator_opt_both ${DELTA_T_EVALUATOR_SOURCE_FILES})
target_compile_definitions(delta_t_evaluator_opt_both PRIVATE OPTIMISTIC_EXECUTION OPTIMISTIC_DISCRETIZATION)
target_link_libraries(delta_t_evaluator_opt_both
        ${WRENCH_LIBRARY}
        ${Boost_LIBRARIES}
        ${WRENCH_WFCOMMONS_WORKFLOW_PARSER_LIBRARY}
)

install(TARGETS redwood_sim redwood_sim_opt_exec redwood_sim_opt_sched redwood_sim_opt_both DESTINATION bin)
install(TARGETS data_parallel_evaluator DESTINATION bin)
install(TARGETS delta_t_evaluator delta_t_evaluator_opt_exec delta_t_evaluator_opt_sched delta_t_evaluator_opt_both DESTINATION bin)