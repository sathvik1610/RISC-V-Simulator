#include "centralized_fetch.hpp"
#include <iostream>

void centralizedFetch(std::vector<PipelinedCore>& cores, const std::vector<std::string>& program) {
    // Iterate through all cores
    for (auto& core : cores) {
        // Skip if core is halted or stalled
        if (core.isHalted()) {
            continue;
        }

        // Check if fetch queue is full (max 2 entries)
        if (core.getFetchQueueSize() >= 2) {
            continue;
        }

        //Check if pipeline is stalled
        if (core.isPipelineStalled()) {
            continue;
        }

        // Get current PC and check if it's valid
        int currentPC = core.getPC();
        if (currentPC >= program.size()) {
            continue;
        }

        // Get the instruction through memory hierarchy if available
        std::string rawInst = program[currentPC];

                // —— hardware barrier support ——
                // If this is our sync opcode, only fetch it once the barrier is open
        // inside centralizedFetch, after you peek rawInst:
        // auto sync = core.getSyncMechanism();              // now resolves
        // if (rawInst == "sync" && sync && !sync->canProceed(core.getCoreId())) {
        //     continue;   // don’t fetch or advance PC until barrier opens
        // }

        if (core.getMemoryHierarchy()) {
            // Use memory hierarchy to fetch the instruction
            // This will access L1I cache and record cache statistics
            auto [latency, _] = core.getMemoryHierarchy()->fetchInstruction(core.getCoreId(), currentPC * 4);
            rawInst = program[currentPC]; // Still use the program vector for the instruction itself

            // If latency > 1, we could simulate a stall here, but we'll keep it simple
        } else {
            // Fall back to direct program access
            rawInst = program[currentPC];
        }

        std::cout << "[Core " << core.getCoreId() << "] Centralized Fetching at PC "
                  << currentPC << ": " << rawInst << std::endl;

        // Create fetch entry with unique ID
        int newId = core.fetchCounter++;
        core.pushToFetchQueue({newId, rawInst});

        // Increment PC and record fetch stage
        core.incrementPC();
        core.recordStageForInstruction(newId, "F");
    }
}