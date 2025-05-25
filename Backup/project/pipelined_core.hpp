#ifndef PIPELINED_CORE_HPP
#define PIPELINED_CORE_HPP

#include <vector>
#include <deque>
#include <string>
#include <unordered_map>
#include <memory>
#include <atomic>
#include "instruction_parser.hpp"
#include "pipeline.hpp"
#include "shared_memory.hpp"
#include "scratchpad.hpp"
#include "sync_barrier.hpp"
#include "Cache.hpp"
#include "memory_hierarchy.hpp"

class MemoryHierarchy;

struct CacheConfig {
    int size;
    int blockSize;
    int associativity;
    int latency;
    Cache::Policy replacementPolicy;
};

struct SpmConfig {
    int size;
    int latency;
};

struct FetchEntry {
    int fetchId;
    std::string rawInst;
};

class PipelinedCore {
public:
    PipelinedCore(int id,
                  std::shared_ptr<SharedMemory> memory,
                  bool enableForwarding,
                  CacheConfig l1i_cfg, CacheConfig l1d_cfg,
                  CacheConfig l2_cfg, SpmConfig spm_cfg,
                  int memLatency, std::shared_ptr<MemoryHierarchy> memHierarchy,SyncBarrier* b);
    SyncBarrier* barrier;
    void reset();
    void clockCycle();
    bool isHalted() const;
    bool isPipelineEmpty() const;
    bool isPipelineStalled() const;
    
    // Cache statistics tracking
    void incrementL1IAccess() { l1i_accesses++; }
    void incrementL1IHit() { l1i_hits++; }
    void incrementL1IMiss() { l1i_misses++; }
    void incrementL1DAccess() { l1d_accesses++; }
    void incrementL1DHit() { l1d_hits++; }
    void incrementL1DMiss() { l1d_misses++; }
    double       getL1DMissRate()     const {
        return l1d_accesses
             ? double(l1d_misses) / l1d_accesses
             : 0.0;
    }
    
    // Cache stall tracking
    void incrementCacheStallCount(int stalls) { cacheStallCount += stalls; }
    
    // Getters for cache statistics
    int getL1IAccessCount() const { return l1i_accesses; }
    int getL1IHitCount() const { return l1i_hits; }
    int getL1IMissCount() const { return l1i_misses; }
    int getL1DAccessCount() const { return l1d_accesses; }
    int getL1DHitCount() const { return l1d_hits; }
    int getL1DMissCount() const { return l1d_misses; }
    double       getL1IMissRate()     const {
        return l1i_accesses
             ? double(l1i_misses) / l1i_accesses
             : 0.0;
    }
    
    // Getters for cache objects
    std::shared_ptr<Cache> getL1ICache() const { return l1ICache; }
    std::shared_ptr<Cache> getL1DCache() const { return l1DCache; }
    std::shared_ptr<Cache> getL2Cache() const { return memoryHierarchy->getL2Cache(); }
    // Reset cache statistics
    void resetCacheStats();
    
    // Cache miss rate calculations
    double getL1ICacheMissRate() const;
    double getL1DCacheMissRate() const;
    double getL2CacheMissRate() const;

    // Getters
    int getPC() const { return pc; }
    int getCoreId() const { return coreId; }
    const std::vector<int>& getRegisters() const { return registers; }
    unsigned long getCycleCount() const { return cycleCount; }
    unsigned long getStallCount() const { return stallCount; }
    unsigned long getCacheStallCount() const { return cacheStallCount; }
    unsigned long getInstructionCount() const { return instructionCount; }
    double getIPC() const;
    
    // Fetch queue operations
    void pushToFetchQueue(const FetchEntry& entry) { fetchQueue.push_back(entry); }
    size_t getFetchQueueSize() const { return fetchQueue.size(); }
    
    // PC operations
    void incrementPC() { pc++; }
    void setPC(int newPC) { pc = newPC; }
    
    // Instruction latency management
    void setInstructionLatency(const std::string& instruction, int latency) {
        pipeline.setInstructionLatency(instruction, latency);
    }
    
    // Forwarding control
    void setForwardingEnabled(bool enabled) { pipeline.setForwardingEnabled(enabled); }
    bool isForwardingEnabled() const { return pipeline.isForwardingEnabled(); }
    
    // Label management
    void setLabels(const std::unordered_map<std::string, int>& lbls);
    const std::unordered_map<std::string, int>& getLabels() const;
    
    // Pipeline visualization
    void exportPipelineRecord(const std::string& filename) const;
    
    // Synchronization
    static std::atomic<int> syncCounter;
    static int totalCores;
    
    // Fetch counter for instruction IDs
    int fetchCounter;

private:
    static const int NUM_REGISTERS = 32;
    
    int coreId;
    std::vector<int> registers;
    std::shared_ptr<SharedMemory> sharedMemory;
    int pc;
    Pipeline pipeline;
    
    // Pipeline stages
    std::deque<FetchEntry> fetchQueue;
    std::deque<Instruction> decodeQueue;
    std::deque<Instruction> executeQueue;
    std::deque<Instruction> memoryQueue;
    std::deque<Instruction> writebackQueue;
    
    // Register forwarding and hazard tracking
    std::unordered_map<int, int> pendingWrites;
    std::unordered_map<int, int> registerAvailableCycle;
    
    // Statistics
    unsigned long cycleCount;
    unsigned long stallCount;
    unsigned long cacheStallCount;
    unsigned long instructionCount;
    
    // Cache statistics
    int l1i_accesses;
    int l1i_hits;
    int l1i_misses;
    int l1d_accesses;
    int l1d_hits;
    int l1d_misses;
    
    // Pipeline state
    bool halted;
    bool cycleStallOccurred;
    
    // Memory hierarchy
    int mainMemoryLatency;
    std::shared_ptr<MemoryHierarchy> memoryHierarchy;
    std::shared_ptr<Cache> l1ICache;
    std::shared_ptr<Cache> l1DCache;
    // std::shared_ptr<Cache> l2Cache;
    std::shared_ptr<Scratchpad> spm;
    
    // Label mapping
    std::unordered_map<std::string, int> labels;
    
    // Pipeline visualization
    std::unordered_map<int, std::vector<std::string>> pipelineRecord;
    
    // Pipeline stage implementations
    void decode(bool& shouldStall);
    void execute(bool& shouldStall);
    void memoryAccess(bool& shouldStall);
    void writeback(bool& shouldStall);
    
    // Helper methods
    bool checkHaltCondition();
    bool isRegisterInUse(int reg) const;
    bool operandsReadyForUse(const Instruction& inst) const;
    bool operandsAvailable(const Instruction& consumer) const;
    bool hasDataHazard(const Instruction& inst) const;
    bool hasControlHazard(const Instruction& inst) const;
    bool canForwardData(const Instruction& consumer, int& rs1Value, int& rs2Value) const;
    
    // Instruction execution helpers
    int executeArithmetic(const Instruction& inst);
    bool executeBranch(const Instruction& inst);
    int executeJump(const Instruction& inst);
    
    // Register access with forwarding
    int getForwardedValue(int reg) const;
    int getRegister(int index) const;
    void setRegister(int index, int value);
    
    // Pipeline visualization helper
    void recordStageForInstruction(int instId, const std::string& stage);
    
};

#endif // PIPELINED_CORE_HPP