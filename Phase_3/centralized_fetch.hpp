#ifndef CENTRALIZED_FETCH_HPP
#define CENTRALIZED_FETCH_HPP

#include <vector>
#include <string>
#include "pipelined_core.hpp"

// Centralized fetch unit that handles instruction fetching for all cores
void centralizedFetch(std::vector<PipelinedCore>& cores, const std::vector<std::string>& program);

#endif // CENTRALIZED_FETCH_HPP