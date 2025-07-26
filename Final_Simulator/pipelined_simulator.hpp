#ifndef PIPELINED_SIMULATOR_HPP
#define PIPELINED_SIMULATOR_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "pipelined_core.hpp"
#include "shared_memory.hpp"
#include "memory_hierarchy.hpp"
#include "sync_mechanism.hpp"

class PipelinedSimulator {
public:
    PipelinedSimulator(int numCores, bool enableForwarding = true);
    
    void loadProgramFromFile(const std::string& filename);
    void loadProgram(const std::string& assembly);
    
    void loadCacheConfig(const std::string& filename);
    
    void setForwardingEnabled(bool enabled);
    bool isForwardingEnabled() const;
    
    void setInstructionLatency(const std::string& instruction, int latency);
    int getInstructionLatency(const std::string& instruction) const;
    
    void run();
    
    void printState() const;
    void printStatistics() const;
    
    bool isExecutionComplete() const;
    
private:
    std::vector<PipelinedCore> cores;
    //std::shared_ptr<SharedMemory> sharedMemory;
    std::shared_ptr<MemoryHierarchy> memoryHierarchy;
    std::shared_ptr<SyncMechanism> syncMechanism;
    
    std::vector<std::string> program;
    std::unordered_map<std::string, int> labelMap;
    std::unordered_map<std::string, int> instructionLatencies;
    bool forwardingEnabled;
};

#endif // PIPELINED_SIMULATOR_HPP