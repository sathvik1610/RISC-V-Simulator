#ifndef PIPELINED_CORE_HPP
#define PIPELINED_CORE_HPP

#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <deque>
#include "shared_memory.hpp"
#include "pipeline.hpp"
#include "instruction_parser.hpp"

class PipelinedCore {
private:
    struct FetchEntry {
        int fetchId;
        std::string rawInst;
    };
    const int coreId;
    bool halted; 
    std::vector<int> registers;
    std::unordered_map<int, int> pendingWrites;
    std::shared_ptr<SharedMemory> sharedMemory;
    int pc;
    static constexpr int NUM_REGISTERS = 32;
    std::unordered_map<std::string, int> labels;
    std::deque<FetchEntry> fetchQueue;
    std::deque<Instruction> decodeQueue;
    std::deque<Instruction> executeQueue;
    std::deque<Instruction> memoryQueue;
    std::deque<Instruction> writebackQueue;
    std::unordered_map<int, int> registerAvailableCycle;

    
    Pipeline pipeline;

    bool hasDataHazard(const Instruction& inst) const;
    bool hasControlHazard(const Instruction& inst) const;
    bool canForwardData(const Instruction& consumer, int& rs1Value, int& rs2Value) const;
    int executeArithmetic(const Instruction& inst);
    int executeMemoryLoad(const Instruction& inst);
    void executeMemoryStore(const Instruction& inst);
    bool executeBranch(const Instruction& inst);
    int executeJump(const Instruction& inst);
    bool cycleStallOccurred = false;
    unsigned long cycleCount;
    unsigned long stallCount;
    unsigned long instructionCount;
    
public:
    PipelinedCore(int id, std::shared_ptr<SharedMemory> memory, bool enableForwarding = true);

    int fetchCounter = 0;
    void reset();
    bool isPipelineStalled() const;
    void recordPipelineState();
    void recordStageForInstruction(int instId, const std::string &stage);
    void exportPipelineRecord(const std::string &filename) const;
    std::unordered_map<int, std::vector<std::string>> pipelineRecord;
    std::unordered_map<int, std::vector<std::string>>& getPipelineRecordNonConst(){
        return pipelineRecord;
    }
    bool isPipelineEmpty() const;
    bool checkHaltCondition();
    bool isHalted() const;
    int getCoreId() const { return coreId; }
    int getPC() const { return pc; }
    int getRegister(int index) const;
    const std::vector<int>& getRegisters() const { return registers; }
    void setRegister(int index, int value);
    void incrementPC() { pc++; }
    size_t getFetchQueueSize() const { return fetchQueue.size();}
    void pushToFetchQueue(const FetchEntry &inst) {
    fetchQueue.push_back({inst.fetchId, inst.rawInst});
    }
    void setForwardingEnabled(bool enabled) { pipeline.setForwardingEnabled(enabled); }
    bool isForwardingEnabled() const { return pipeline.isForwardingEnabled(); }
    void setInstructionLatency(const std::string& instruction, int latency){
        pipeline.setInstructionLatency(instruction, latency);
    }
    void fetch(const std::vector<std::string>& program, bool& shouldStall);
    void decode(bool& shouldStall);

    bool operandsReadyForUse(const Instruction &inst) const;

    bool isRegisterInUse(int reg) const;

    int getForwardedValue(int reg) const;
    bool operandsAvailable(const Instruction &consumer) const;
    void execute(bool &shouldStall);
    void memory(bool &shouldStall);
    void memoryAccess(bool& shouldStall);
    void writeback(bool& shouldStall);
    void clockCycle();
    unsigned long getCycleCount() const { return cycleCount; }
    unsigned long getStallCount() const { return stallCount; }
    unsigned long getInstructionCount() const { return instructionCount; }
    double getIPC() const;
    void setLabels(const std::unordered_map<std::string, int>& lbls);
    const std::unordered_map<std::string, int>& getLabels() const;
};
#endif