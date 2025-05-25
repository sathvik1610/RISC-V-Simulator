#ifndef CENTRALIZED_FETCH_HPP
#define CENTRALIZED_FETCH_HPP

#include <vector>
#include <memory>
#include <string>
#include "pipelined_core.hpp"
#include "memory_hierarchy.hpp"

// Function to implement centralized fetch across all cores
void centralizedFetch(
    std::vector<PipelinedCore>& cores,
    const std::vector<std::string>& program,
    const std::shared_ptr<MemoryHierarchy>& memoryHierarchy
);

#endif // CENTRALIZED_FETCH_HPP