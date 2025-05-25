#ifndef PIPELINE_HPP
#define PIPELINE_HPP

#include <string>
#include <unordered_map>
#include <vector>
#include <queue>
#include <memory>

enum class PipelineStage {
    FETCH,
    DECODE,
    EXECUTE,
    MEMORY,
    WRITEBACK,
    COMPLETED
};

/* struct Instruction {
    int id{-1};
    std::string raw;
    std::string opcode;
    int rd = -1;
    int rs1 = -1;
    int rs2 = -1;
    int rs1Value = 0;
    int rs2Value = 0;
    int immediate = 0;
    bool isBranch = false;
    bool isJump = false;
    bool isMemory = false;
    bool isArithmetic = false;
    bool takeBranch = false;
    int targetPC = -1;
    int coreId = -1;
    bool shouldExecute = true;
    std::string label;
    bool useCID = false;
    PipelineStage stage = PipelineStage::FETCH;
    int cyclesInExecute = 0;
    int executeLatency = 1;
    int resultValue = 0;
    bool hasResult = false;
    bool isScratchpadMemory = false;
    bool waitingForMemory = false;
    int totalMemoryLatency = 0;
    bool isSPM = false;
    bool isSync = false;
    int memoryLatency = 0;
    bool isHalt = false;
    bool dirty = false;  // For write-back policy
    bool valid = true;   // For cache validity
}; */

class Pipeline {
private:
    std::vector<Instruction> stages;
    std::unordered_map<std::string, int> instructionLatencies;
    bool forwardingEnabled;
    int stallCount;
    int instructionCount;
    int cacheStallCount;
    int memoryStallCount;
    bool writeBackEnabled;
    bool writeAllocateEnabled;
    
public:
    Pipeline(bool enableForwarding = true) 
        : forwardingEnabled(enableForwarding), 
          stallCount(0), 
          instructionCount(0),
          cacheStallCount(0),
          memoryStallCount(0),
          writeBackEnabled(true),
          writeAllocateEnabled(true) {
        stages.resize(5);
        
        instructionLatencies = {
            {"add", 1}, {"addi", 1}, {"sub", 1}, {"slt", 1},
            {"mul", 3}, {"lw", 1}, {"sw", 1},
            {"lw_spm", 1}, {"sw_spm", 1}
        };
    }
    
    void setInstructionLatency(const std::string& instruction, int latency) {
        instructionLatencies[instruction] = latency;
    }
    
    int getInstructionLatency(const std::string& instruction) const {
        auto it = instructionLatencies.find(instruction);
        return it != instructionLatencies.end() ? it->second : 1;
    }

    void setCachePolicy(bool writeBack, bool writeAllocate) {
        writeBackEnabled = writeBack;
        writeAllocateEnabled = writeAllocate;
    }

    bool isWriteBackEnabled() const { return writeBackEnabled; }
    bool isWriteAllocateEnabled() const { return writeAllocateEnabled; }
    
    void incrementCacheStallCount() { cacheStallCount++; }
    void incrementMemoryStallCount() { memoryStallCount++; }
    int getCacheStallCount() const { return cacheStallCount; }
    int getMemoryStallCount() const { return memoryStallCount; }
    
    void setForwardingEnabled(bool enabled) { forwardingEnabled = enabled; }
    bool isForwardingEnabled() const { return forwardingEnabled; }
    
    int getStallCount() const { return stallCount; }
    int getInstructionCount() const { return instructionCount; }
    
    double getIPC() const {
        if (instructionCount == 0) return 0.0;
        return static_cast<double>(instructionCount) / 
               (instructionCount + stallCount + cacheStallCount + memoryStallCount);
    }
    
    void incrementStallCount() { stallCount++; }
    void incrementInstructionCount() { instructionCount++; }
    
    void reset() {
        stallCount = 0;
        instructionCount = 0;
        cacheStallCount = 0;
        memoryStallCount = 0;
        stages.clear();
        stages.resize(5);
    }

    const std::vector<Instruction>& getStages() const { return stages; }
    std::vector<Instruction>& getStages() { return stages; }
};

#endif // PIPELINE_HPP
