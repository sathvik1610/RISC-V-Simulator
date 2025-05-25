#include "centralized_fetch.hpp"
#include "instruction_parser.hpp"
#include <iostream>
#include "pipelined_core.hpp"


void centralizedFetch(std::vector<PipelinedCore>& cores, const std::vector<std::string>& program) {
    for (auto& core : cores) {
        if (core.isHalted()) {
            continue;
        }
        if (core.getFetchQueueSize() >= 2) {
            continue;
        }
        if (core.isPipelineStalled()) {
            continue;
        }

        int currentPC = core.getPC();
        if (currentPC >= program.size()) {
            continue;  
        }

        std::string rawInst = program[currentPC];

        std::cout << "[Core " << core.getCoreId() << "] Centralized Fetching at PC "
                  << currentPC << ": " << rawInst << std::endl;

        std::string inst = rawInst;

        int newId = core.fetchCounter++;  
        core.pushToFetchQueue({newId, rawInst});
        core.incrementPC();
        core.recordStageForInstruction(newId,"F");

    }
}