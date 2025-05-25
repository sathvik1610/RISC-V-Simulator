cmake_minimum_required(VERSION 3.31)
project(project)

set(CMAKE_CXX_STANDARD 17)

include_directories(.)

add_executable(project
        cache.hpp
        cache.cpp
        cache_system.hpp
        centralized_fetch.cpp
        centralized_fetch.hpp
        instruction_parser.hpp
        main.cpp
        memory_hierarchy.hpp
        memory_hierarchy.cpp
        pipeline.hpp
        pipelined_core.cpp
        pipelined_core.hpp
        pipelined_simulator.cpp
        pipelined_simulator.hpp
        scratchpad_memory.hpp
        shared_memory.hpp
        sync_mechanism.hpp)
