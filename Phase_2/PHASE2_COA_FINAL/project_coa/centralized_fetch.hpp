#ifndef CENTRALIZED_FETCH_HPP
#define CENTRALIZED_FETCH_HPP

#include "pipelined_core.hpp"
#include <vector>
#include <string>


void centralizedFetch(std::vector<PipelinedCore>& cores, const std::vector<std::string>& program);

#endif 
