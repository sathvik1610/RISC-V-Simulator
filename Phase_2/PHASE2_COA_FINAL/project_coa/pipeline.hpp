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

struct Instruction {
    int id; // New unique ID field

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
};

class Pipeline {
private:
    std::vector<Instruction> stages;
    std::unordered_map<std::string, int> instructionLatencies;
    bool forwardingEnabled;
    int stallCount;
    int instructionCount;
    
public:
    Pipeline(bool enableForwarding = true) 
        : forwardingEnabled(enableForwarding), stallCount(0), instructionCount(0) {
        // Initialize pipeline stages
        stages.resize(5); // 5 stages: F, D, E, M, W
        
        // Default latencies
        instructionLatencies["add"] = 1;
        instructionLatencies["addi"] = 1;
        instructionLatencies["sub"] = 1;
        instructionLatencies["slt"] = 1;
        instructionLatencies["mul"] = 3;
    }
    
    void setInstructionLatency(const std::string& instruction, int latency) {
        instructionLatencies[instruction] = latency;
    }
    
    int getInstructionLatency(const std::string& instruction) const {
        auto it = instructionLatencies.find(instruction);
        if (it != instructionLatencies.end()) {
            return it->second;
        }
        return 1; // Default latency
    }
    
    void setForwardingEnabled(bool enabled) {
        forwardingEnabled = enabled;
    }
    
    bool isForwardingEnabled() const {
        return forwardingEnabled;
    }
    
    int getStallCount() const {
        return stallCount;
    }
    
    int getInstructionCount() const {
        return instructionCount;
    }
    
    double getIPC() const {
        if (instructionCount == 0) return 0.0;
        return static_cast<double>(instructionCount) / (instructionCount + stallCount);
    }
    
    void incrementStallCount() {
        stallCount++;
    }
    
    void incrementInstructionCount() {
        instructionCount++;
    }
    
    void reset() {
        stallCount = 0;
        instructionCount = 0;
        stages.clear();
        stages.resize(5);
    }
};

#endif