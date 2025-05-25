#include "centralized_fetch.hpp"
#include <iostream>
#include <unordered_map>

void centralizedFetch(
    std::vector<PipelinedCore> &cores,
    const std::vector<std::string> &program,
    const std::shared_ptr<MemoryHierarchy> &memoryHierarchy)
{
    // Cache for fetched instructions to avoid redundant memory accesses
    std::unordered_map<uint32_t, MemoryHierarchy::Access> fetchCache;

    // Process each core
    for (auto &core : cores)
    {
        // Skip if core is halted or fetch queue is full
        if (core.isHalted() ||
            core.getFetchQueueSize() >= 2 ||
            core.isPipelineStalled())
        {
            continue;
        }

        // Get current PC and check bounds
        int pc = core.getPC();
        if (pc >= static_cast<int>(program.size()))
        {
            continue;
        }

        // Convert PC to byte address
        // uint32_t addr = pc * 4;
        uint64_t raw_addr = core.getPC() * 4;
        uint64_t block_size = core.getL1ICache()->getBlockSize();
        // align down to start of block:
        uint64_t addr = raw_addr & ~(block_size - 1);
        // Check if instruction was already fetched by another core
        // if (fetchCache.count(addr)) {
        //     // Use cached fetch result
        //     core.incrementL1IAccess();
        //      core.incrementL1IHit();
        //     auto& cachedAccess = fetchCache[addr];

        //     // Update this core's L1I cache
        //     core.getL1ICache()->loadBlockFromMemory(
        //         addr,
        //         memoryHierarchy->getRawMemoryPtr(),
        //         core.getCoreId()
        //     );

        //     // Apply latency penalty if it was a miss
        //     if (!cachedAccess.hit) {
        //         core.incrementCacheStallCount(cachedAccess.latency);
        //         continue;
        //     }
        // }
        //  else {
        // Try L1I cache first
        auto l1i = core.getL1ICache();
        std::cout << "hihi " << raw_addr << std::endl;

        auto access = l1i->read(addr);
        core.incrementL1IAccess();
        if (!access.hit)

        {
            core.incrementL1IMiss();

            // L1I miss, try memory hierarchy
            auto memAccess = memoryHierarchy->fetchInstruction(addr, core.getCoreId());
            // fetchCache[addr] = memAccess;
            core.getL1ICache()->loadBlockFromMemory(
                addr,
                memoryHierarchy->getRawMemoryPtr(),
                core.getCoreId());
            // Apply miss penalty
            if (!memAccess.hit)
            {
                int penalty = memAccess.latency - l1i->getAccessLatency();
                if (penalty > 0)
                {
                    core.incrementCacheStallCount(penalty);
                    std::cout << "[Core " << core.getCoreId()
                              << "] Instruction cache miss at PC " << pc
                              << ", stalling for " << penalty << " cycles\n";
                }
            }
            core.pushToFetchQueue({core.fetchCounter++, program[pc]});
            core.incrementPC();
            continue;
        }
        // }
        core.incrementL1IHit();
        // Fetch successful, create fetch entry and advance PC
        core.pushToFetchQueue({core.fetchCounter++, program[pc]});
        core.incrementPC();

        std::cout << "[Core " << core.getCoreId() << "] Fetched instruction at PC "
                  << pc << ": " << program[pc] << "\n";
    }
}