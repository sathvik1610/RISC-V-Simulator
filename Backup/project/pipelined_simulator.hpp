#ifndef PIPELINED_SIMULATOR_HPP
#define PIPELINED_SIMULATOR_HPP

#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include "pipelined_core.hpp"
#include "shared_memory.hpp"
#include "cache.hpp"
#include "memory_hierarchy.hpp"
#include "scratchpad.hpp"
#include "sync_barrier.hpp"

class PipelinedSimulator {
public:
    // Constructor with detailed cache and memory configuration
    PipelinedSimulator(int numCores = 4,
                       int l1Size = 4096, int l1BlockSize = 64, int l1Assoc = 2, int l1Latency = 1,
                       int l2Size = 16384, int l2BlockSize = 64, int l2Assoc = 4, int l2Latency = 5,
                       int memLatency = 100, int spmSize = 4096, int spmLatency = 1);

    // Program loading
    void loadProgramFromFile(const std::string &filename);
    void loadProgram(const std::string &assembly);

    // Cache policy configuration
    void setCachePolicy(Cache::Policy policy);
    void setWritePolicy(WritePolicy policy);
    void setWriteAllocatePolicy(WriteAllocatePolicy policy);

    // Pipeline configuration
    void setForwardingEnabled(bool enabled);
    bool isForwardingEnabled() const;
    void setInstructionLatency(const std::string &instruction, int latency);
    int getInstructionLatency(const std::string &instruction) const;

    // Simulation execution
    void run();
    bool isExecutionComplete() const;

    // Output methods
    void printState() const;
    void printStatistics() const;

private:
    std::vector<PipelinedCore> cores;
    std::shared_ptr<SharedMemory> sharedMemory;
    std::shared_ptr<Scratchpad> scratchpad;
    std::shared_ptr<MemoryHierarchy> memoryHierarchy;
    std::shared_ptr<SyncBarrier> syncBarrier;
    std::vector<std::string> program;
    std::unordered_map<std::string, int> labelMap;
    std::unordered_map<std::string, int> instructionLatencies;
    bool forwardingEnabled;
};

#endif // PIPELINED_SIMULATOR_HPP