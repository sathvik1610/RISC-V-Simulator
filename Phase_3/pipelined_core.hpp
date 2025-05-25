#ifndef PIPELINED_CORE_HPP
#define PIPELINED_CORE_HPP

#include <vector>
#include <deque>
#include <unordered_map>
#include <memory>
#include "pipeline.hpp"
#include "shared_memory.hpp"
#include "memory_hierarchy.hpp"
#include "sync_mechanism.hpp"

struct FetchEntry {
    int fetchId;
    std::string rawInst;
};

class PipelinedCore {
public:
    static constexpr int NUM_REGISTERS = 32;

    PipelinedCore(int id, bool enableForwarding = true);
    
    void reset();
    int getCoreId() const { return coreId; }
    int getPC() const { return pc; }
    void incrementPC() { pc++; }
    void setPC(int newPC) { pc = newPC; }
    bool isHalted() const;
    
    bool isPipelineEmpty() const;
    bool isPipelineStalled() const;
    
    void pushToFetchQueue(const FetchEntry& entry) { fetchQueue.push_back(entry); }
    void recordStageForInstruction(int instId, const std::string& stage);
    void clockCycle();
    
    int getFetchQueueSize() const { return fetchQueue.size(); }
    void exportPipelineRecord(const std::string& filename) const;
    
    int getRegister(int index) const;
    void setRegister(int index, int value);
    
    const std::vector<int>& getRegisters() const { return registers; }
    
    int getCycleCount() const { return cycleCount; }
    int getStallCount() const { return stallCount; }
    int getInstructionCount() const { return instructionCount; }
    double getIPC() const;
    
    void setLabels(const std::unordered_map<std::string, int>& lbls);
    const std::unordered_map<std::string, int>& getLabels() const;
    
    void setInstructionLatency(const std::string& instruction, int latency) {
        pipeline.setInstructionLatency(instruction, latency);
    }
    
    void setForwardingEnabled(bool enabled) {
        pipeline.setForwardingEnabled(enabled);
    }
    int fetchWaitCyclesRemaining = 0;
    bool fetchInProgress = false;

    int fetchCounter = 0;
    void setMemoryHierarchy(std::shared_ptr<MemoryHierarchy> memHierarchy) {
        this->memoryHierarchy = memHierarchy;
    }
    
    void setSyncMechanism(std::shared_ptr<SyncMechanism> syncMech) {
        this->syncMechanism = syncMech;
    }
    
    int getMemoryStallCycles() const { return pipeline.getMemoryStallCycles(); }
    std::shared_ptr<MemoryHierarchy> getMemoryHierarchy() const {
        return memoryHierarchy;
    }
    int getRemainingFetchWaitCycles() const {
        return fetchWaitCycles;
    }

    void decrementFetchWaitCycles() {
        if (fetchWaitCycles > 0) {
            fetchWaitCycles--;
        }
    }

    void setFetchWait(const std::string& inst, int waitCycles) {
        fetchWaitCycles = waitCycles;
        pendingFetchInstruction = inst;
        hasPendingFetch = true;
    }

    bool hasPendingInstructionToPush() const {
        return hasPendingFetch && fetchWaitCycles == 0;
    }

    const std::string& getPendingFetchInstruction() const {
        return pendingFetchInstruction;
    }

    void clearPendingFetch() {
        hasPendingFetch = false;
        pendingFetchInstruction.clear();
    }

    void incrementMemoryStall() {
        stallCount++;
        pipeline.incrementMemoryStallCycles(1);
    }
    bool fetchQueueEmpty() const {
        return fetchQueue.empty();
    }
    bool memoryQueueEmpty() const {
        return memoryQueue.empty();
    }

    bool writebackQueueEmpty() const {
        return writebackQueue.empty();
    }
    std::shared_ptr<SyncMechanism> getSyncMechanism() const {
        return syncMechanism;
    }

private:

   // bool terminated = false;
    int coreId;
    std::vector<int> registers;
    //std::shared_ptr<SharedMemory> sharedMemory;
    std::shared_ptr<MemoryHierarchy> memoryHierarchy;
    std::shared_ptr<SyncMechanism> syncMechanism;
    int pc;
    Pipeline pipeline;
    int fetchWaitCycles = 0;
    std::string pendingFetchInstruction;
    bool hasPendingFetch = false;

    
    std::deque<FetchEntry> fetchQueue;
    std::deque<Instruction> decodeQueue;
    std::deque<Instruction> executeQueue;
    std::deque<Instruction> memoryQueue;
    std::deque<Instruction> writebackQueue;
    
    std::unordered_map<int, int> pendingWrites;
    std::unordered_map<int, int> registerAvailableCycle;
    
    std::unordered_map<std::string, int> labels;
    std::unordered_map<int, std::vector<std::string>> pipelineRecord;
    
    int cycleCount;
    int stallCount;
    int instructionCount;
    bool cycleStallOccurred;
    bool halted;
    
    void decode(bool &shouldStall);

    //int executeArithmetic(int _cpp_par_, int _cpp_par_, int _cpp_par_, int _cpp_par_);

    void execute(bool &shouldStall);
    void memoryAccess(bool &shouldStall);
    void writeback(bool &shouldStall);
    
    bool checkHaltCondition();
    bool hasDataHazard(const Instruction &inst) const;
    bool hasControlHazard(const Instruction &inst) const;
    bool isRegisterInUse(int reg) const;
    bool operandsReadyForUse(const Instruction &inst) const;
    bool operandsAvailable(const Instruction &consumer) const;
    
    int getForwardedValue(int reg) const;
    bool canForwardData(const Instruction &consumer, int &rs1Value, int &rs2Value) const;

    int executeArithmetic(int op1, int op2, int imm, const std::string &opcode);

    //  int executeArithmetic(const Instruction &inst);

    //  int executeArithmetic(const Instruction &inst);
    bool executeBranch(const Instruction &inst);
    int executeJump(const Instruction &inst);
};

#endif // PIPELINED_CORE_HPP