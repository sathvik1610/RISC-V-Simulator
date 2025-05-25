cmake_minimum_required(VERSION 3.31)
project(project)

set(CMAKE_CXX_STANDARD 14)

include_directories(.)

add_executable(project
        cache.hpp
        centralized_fetch.cpp
        centralized_fetch.hpp
        instruction_parser.hpp
        main.cpp
        memory_hierarchy.hpp
        pipeline.hpp
        pipelined_core.cpp
        pipelined_core.hpp
        pipelined_simulator.cpp
        pipelined_simulator.hpp
        scratchpad.hpp
        shared_memory.hpp
        sync_barrier.hpp)
