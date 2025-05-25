cmake_minimum_required(VERSION 3.29)
project(project_coa)

set(CMAKE_CXX_STANDARD 14)

include_directories(.)

add_executable(project_coa
    core.cpp
    core.hpp
    main.cpp
    simulator.cpp
    simulator.hpp
        shared_memory.h)
