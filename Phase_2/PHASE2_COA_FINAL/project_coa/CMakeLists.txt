cmake_minimum_required(VERSION 3.29)
project(project_coa)

set(CMAKE_CXX_STANDARD 17)

include_directories(.)

add_executable(project_coa
        centralized_fetch.cpp
        instruction_parser.hpp
        pipelined_simulator.cpp
        pipelined_core.cpp
        pipelined_simulator.hpp
        pipelined_core.hpp
        main.cpp
        centralized_fetch.hpp
        pipeline.hpp
        shared_memory.hpp
)
