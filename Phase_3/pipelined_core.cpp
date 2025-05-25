#include "pipelined_core.hpp"
#include "instruction_parser.hpp"
#include <stdexcept>
#include <algorithm>
#include <iostream>
#include <unordered_set>
#include <fstream>
#include <iomanip>
#include <vector>
#include <unordered_map>

PipelinedCore::PipelinedCore(int id, bool enableForwarding)
    : coreId(id)
      , registers(NUM_REGISTERS, 0)
      , pc(0)
      , pipeline(enableForwarding)
      , cycleCount(0)
      , stallCount(0)
      , instructionCount(0)
      , halted(false) {
    registers[31] = coreId;
}

void PipelinedCore::reset() {
    std::fill(registers.begin(), registers.end(), 0);
    registers[31] = coreId;
    pc = 0;
    labels.clear();

    fetchQueue.clear();
    decodeQueue.clear();
    executeQueue.clear();
    memoryQueue.clear();
    writebackQueue.clear();
    pendingWrites.clear();

    cycleCount = 0;
    stallCount = 0;
    instructionCount = 0;
    pipeline.reset();
}

int PipelinedCore::getForwardedValue(int reg) const {
    if (!pipeline.isForwardingEnabled()) {
        return getRegister(reg);
    }
    for (const auto &inst: writebackQueue) {
        if (inst.hasResult && inst.rd == reg)
            return inst.resultValue;
    }
    for (const auto &inst: memoryQueue) {
        if (inst.hasResult && inst.rd == reg)
            return inst.resultValue;
    }
    for (const auto &inst: executeQueue) {
        if (inst.hasResult && inst.rd == reg)
            return inst.resultValue;
    }
    return getRegister(reg);
}

void PipelinedCore::exportPipelineRecord(const std::string &filename) const {
    std::ofstream outFile(filename);
    if (!outFile.is_open()) {
        std::cerr << "Error opening file " << filename << std::endl;
        return;
    }

    outFile << "InstrID";
    for (int cycle = 0; cycle <= cycleCount; cycle++) {
        outFile << ",Cycle" << (cycle+1);
    }
    outFile << "\n";

    std::vector<int> keys;
    for (const auto &pair: pipelineRecord) {
        keys.push_back(pair.first);
    }
    std::sort(keys.begin(), keys.end());

    std::unordered_map<int, int> idMapping;
    int normalizedId = 1;
    for (int id: keys) {
        idMapping[id] = normalizedId++;
    }

    for (auto &pair: const_cast<std::unordered_map<int, std::vector<std::string>> &>(pipelineRecord)) {
        while (pair.second.size() < static_cast<size_t>(cycleCount)) {
            pair.second.push_back("");
        }
    }

    for (int id: keys) {
        outFile << idMapping[id];
        auto it = pipelineRecord.find(id);
        if (it != pipelineRecord.end()) {
            for (const auto &stage: it->second) {
                outFile << "," << stage;
            }
        }
        outFile << "\n";
    }

    outFile.close();
    std::cout << "Pipeline record exported to " << filename << std::endl;
}

bool PipelinedCore::isPipelineStalled() const {
    if (cycleStallOccurred)
        return true;

    if (fetchQueue.size() >= 2)
        return true;
    if (decodeQueue.size() >= 2)
        return true;
    if (memoryQueue.size() >= 2)
        return true;
    if (writebackQueue.size() >= 2)
        return true;

    return false;
}

void PipelinedCore::recordStageForInstruction(int instId, const std::string &stage) {
    if (pipelineRecord.find(instId) == pipelineRecord.end()) {
        pipelineRecord[instId] = std::vector<std::string>(cycleCount, "");
    }

    pipelineRecord[instId].push_back(stage);
}

int PipelinedCore::getRegister(int index) const {
    if (index < 0 || index >= NUM_REGISTERS) {
        throw std::out_of_range("Register index out of range");
    }
    if (index == 31) {
        return coreId;
    }
    if (index == 0) {
        return 0;
    }
    return registers[index];
}

void PipelinedCore::setRegister(int index, int value) {
    if (index < 0 || index >= NUM_REGISTERS) {
        throw std::out_of_range("Register index out of range");
    }
    if (index != 0 && index != 31) {
        registers[index] = value;
    }
}

void PipelinedCore::decode(bool &shouldStall) {
    shouldStall = false;
      if (halted) {
              shouldStall = false;
               return;
           }
    if (fetchQueue.empty()) {
        return;
    }

    if (cycleStallOccurred) {
        shouldStall = true;
        return;
    }

    if (decodeQueue.size() >= 2) {
        const FetchEntry &entry = fetchQueue.front();

        recordStageForInstruction(entry.fetchId, "S");
        shouldStall = true;
        cycleStallOccurred = true;
        stallCount++;
        return;
    }

    auto entry = fetchQueue.front();
    // if (entry.rawInst.rfind("sync",0)==0) {
    //     syncMechanism->arrive(coreId);
    //
    //     // if not everyone’s here yet, stall this core:
    //     if (!syncMechanism->allArrived()) {
    //         recordStageForInstruction(entry.fetchId,"S");
    //         shouldStall = true;
    //         return;  // leave the sync in fetchQueue to retry next cycle
    //     }
    //
    //     // once allArrived==true, consume the sync as a no‑op:
    //     syncMechanism->reset();        // re‑arm for next barrier
    //     // fall through: pop fetchQueue & push a “nop” sync inst into the pipeline
    // }

    if (entry.rawInst.find(':') != std::string::npos) {
        incrementPC();
        fetchQueue.pop_front();
        return;
    }

    Instruction inst = InstructionParser::parseInstruction(entry.rawInst, coreId);

    if (!pipeline.isForwardingEnabled() && !operandsReadyForUse(inst)) {
        recordStageForInstruction(entry.fetchId, "S");
        shouldStall = true;
        cycleStallOccurred = true;
        stallCount++;
        return;
    }

    inst.id = entry.fetchId;
    inst.shouldExecute = true;
    fetchQueue.pop_front();

    // if (inst.opcode == "beq") {
    //     int targetCID = inst.rs2;
    //     if (coreId != targetCID) {
    //         inst.shouldExecute = false;
    //     }
    // }
    if (inst.opcode == "beq" && inst.rs1 == 31) {
        // only CU inst.rs2 should ever execute it:
        if (coreId != inst.rs2) inst.shouldExecute = false;
    }

    if (inst.isArithmetic) {
        inst.executeLatency = pipeline.getInstructionLatency(inst.opcode);
    }

    decodeQueue.push_back(inst);
    recordStageForInstruction(inst.id, "D");
}

bool PipelinedCore::isRegisterInUse(int reg) const {
    if (reg == 0) {
        return false;
    }

    for (const auto &inst: decodeQueue) {
        if (inst.rd == reg && inst.shouldExecute) {
            return true;
        }
    }

    for (const auto &inst: executeQueue) {
        if (inst.rd == reg && inst.shouldExecute) {
            return true;
        }
    }

    for (const auto &inst: memoryQueue) {
        if (inst.rd == reg && inst.shouldExecute) {
            return true;
        }
    }

    for (const auto &inst: writebackQueue) {
        if (inst.rd == reg && inst.shouldExecute) {
            return true;
        }
    }

    return false;
}

bool PipelinedCore::operandsReadyForUse(const Instruction &inst) const {
    for (int reg: {inst.rs1, inst.rs2}) {
        if (reg == 0) continue;

        auto it = registerAvailableCycle.find(reg);
        if (it != registerAvailableCycle.end()) {
            if (cycleCount < it->second) {
                return false;
            }
        }
    }
    return true;
}

bool PipelinedCore::operandsAvailable(const Instruction &consumer) const {
    static int callCount = 0;
    callCount++;

    if (!pipeline.isForwardingEnabled()) {
        if (pendingWrites.find(consumer.rs1) != pendingWrites.end() ||
            pendingWrites.find(consumer.rs2) != pendingWrites.end()) {
            return false;
        }

        auto checkQueue = [this, &consumer](const std::deque<Instruction> &q, const std::string &qName) -> bool {
            for (const auto &inst: q) {
                if (inst.id == consumer.id)
                    continue;
                if (inst.rd < 0)
                    continue;
                if (inst.rd == 0)
                    continue;
                if ((consumer.rs1 != 0 && inst.rd == consumer.rs1) ||
                    (consumer.rs2 != 0 && inst.rd == consumer.rs2)) {
                    std::cout << "  BLOCKING: inst id=" << inst.id
                            << " (rd=" << inst.rd << ") in " << qName
                            << " blocks consumer (id=" << consumer.id << ")" << std::endl;
                    return false;
                }
            }
            return true;
        };

        if (!checkQueue(decodeQueue, "decodeQueue"))
            return false;
        if (!checkQueue(executeQueue, "executeQueue"))
            return false;
        if (!checkQueue(memoryQueue, "memoryQueue"))
            return false;
        if (!checkQueue(writebackQueue, "writebackQueue"))
            return false;

        return true;
    }
    else {
        auto checkQueueForward = [this, &consumer](const std::deque<Instruction> &q, const std::string &qName) -> bool {
            for (const auto &inst: q) {
                if (inst.id == consumer.id)
                    continue;
                if (inst.rd < 0)
                    continue;
                if (inst.rd == 0) continue;
                if (!inst.hasResult && ((consumer.rs1 != 0 && inst.rd == consumer.rs1) ||
                                        (consumer.rs2 != 0 && inst.rd == consumer.rs2))) {
                    std::cout << "  BLOCKING: inst id=" << inst.id
                            << " (rd=" << inst.rd << ") in " << qName
                            << " blocks consumer (id=" << consumer.id << ")" << std::endl;
                    return false;
                }
            }
            return true;
        };

        if (!checkQueueForward(decodeQueue, "decodeQueue"))
            return false;
        if (!checkQueueForward(executeQueue, "executeQueue"))
            return false;
        if (!checkQueueForward(memoryQueue, "memoryQueue"))
            return false;
        if (!checkQueueForward(writebackQueue, "writebackQueue"))
            return false;

        return true;
    }
}

void PipelinedCore::execute(bool &shouldStall) {
    shouldStall = false;
    Instruction inst;
    bool fromDecode = false;

    if (!executeQueue.empty()) {
        inst = executeQueue.front();
        executeQueue.pop_front();
    }
    else if (!decodeQueue.empty()) {
        if (!pipeline.isForwardingEnabled() && !operandsAvailable(decodeQueue.front())) {
            recordStageForInstruction(decodeQueue.front().id, "S");
            cycleStallOccurred = true;
            shouldStall = true;
            stallCount++;
            return;
        }
        inst = decodeQueue.front();
        decodeQueue.pop_front();
        fromDecode = true;
    }
    else {
        return;
    }

    recordStageForInstruction(inst.id, "E");

    std::cout << "[Core " << coreId << "] Executing instruction: "
            << inst.opcode << " (rs1: " << inst.rs1
            << ", rs2: " << inst.rs2 << ", rd: " << inst.rd << ")\n";
    std::cout << "   Clock cycle : " << cycleCount << std::endl;

    if (!inst.shouldExecute) {
        std::cout << "[Core " << coreId << "] Skipping instruction (shouldExecute = false)\n";
        memoryQueue.push_back(inst);
        return;
    }
    if (inst.opcode == "halt") {
        std::cout << "[Core " << coreId << "] Executing HALT, flushing pipeline\n";
        fetchQueue.clear();
        decodeQueue.clear();
        executeQueue.clear();
        memoryQueue.clear();
        halted=true;
        recordStageForInstruction(inst.id, "M");
        return;
    }
    // Handle SYNC instruction
    std::cout << "[Core " << coreId << "] About to check SYNC execution\n";

    // if (inst.isSync) {
    //     std::cout << "[Core " << coreId << "] Executing SYNC instruction\n";
    //
    //     // Call sync mechanism
    //     if (syncMechanism) {
    //         syncMechanism->syncCore(coreId);
    //     }
    //
    //     // Move to memory stage
    //     memoryQueue.push_back(inst);
    //     return;
    // }
        if (inst.isSync) {
                // Phase 1: mark arrival, stall until all cores have arrived
                std::cout << "[Core " << coreId << "] Arrived at SYNC\n";
              syncMechanism->arrive(coreId);

                if (!syncMechanism->canProceed(coreId)) {
                        // still waiting for the last core → spin here
                        recordStageForInstruction(inst.id, "S");
                        shouldStall = true;
                    cycleStallOccurred = true;
                    executeQueue.push_front(inst);
                        return;
                  }

                // Phase 1 complete: push the SYNC into MEM so it can retire
                std::cout << "[Core " << coreId << "] Barrier complete, advancing\n";
           // syncMechanism->reset();
                recordStageForInstruction(inst.id, "E");
                memoryQueue.push_back(inst);
                return;
           }



    if (inst.isArithmetic) {
        int op1 = 0, op2 = 0;

        if (fromDecode) {
            op1 = getForwardedValue(inst.rs1);
            op2 = (inst.opcode == "addi") ? inst.immediate : getForwardedValue(inst.rs2);
        } else {
            // ✨ FIX: use register values, not IDs
            op1 = registers[inst.rs1];
            op2 = (inst.opcode == "addi") ? inst.immediate : registers[inst.rs2];
        }

        inst.resultValue = executeArithmetic(op1, op2, inst.immediate, inst.opcode);
        inst.hasResult = true;
        if (pipeline.isForwardingEnabled() && inst.rd > 0) {
            setRegister(inst.rd, inst.resultValue);
            registerAvailableCycle[inst.rd] = cycleCount;
        }

        std::cout << "[Core " << coreId << "] Arithmetic result: "
                  << inst.resultValue << "\n";
        std::cout << "    Clock cycle : " << cycleCount << std::endl;

        if (inst.executeLatency > 1) {
            inst.cyclesInExecute++;
            if (inst.cyclesInExecute < inst.executeLatency) {
                std::cout << "[Core " << coreId << "] Multi-cycle arithmetic: cycle "
                          << inst.cyclesInExecute << " of " << inst.executeLatency << "\n";
                executeQueue.push_back(inst);
                stallCount++;
                shouldStall = true;
                return;
            }
        }
    }

    else if (inst.isMemory && (inst.opcode == "lw" || inst.opcode == "lw_spm")) {
        int base = getForwardedValue(inst.rs1);
        int effectiveAddress = base + inst.immediate;

        inst.resultValue = effectiveAddress;

        std::cout << "[Core " << coreId << "] Memory load address calculated: "
                << effectiveAddress << "\n";
        std::cout << "    Clock cycle : " << cycleCount << std::endl;
    }
    else if (inst.isMemory && (inst.opcode == "sw" || inst.opcode == "sw_spm")) {
        if (inst.opcode == "sw") {
            std::cout << "[Core " << coreId << "] Executing SW: x" << inst.rs2
                      << " (val=" << registers[inst.rs2] << ") to mem["
                      << registers[inst.rs1] + inst.immediate << "]\n";
        }

        int base = getForwardedValue(inst.rs1);
        int valueToStore = getForwardedValue(inst.rs2);

        int effectiveAddress = base + inst.immediate;

        inst.rs1 = effectiveAddress;
        inst.rs2 = valueToStore;

        std::cout << "[Core " << coreId << "] Memory store address calculated: "
                << effectiveAddress << ", Value to store: " << valueToStore << "\n";
        std::cout << "    Clock cycle : " << cycleCount << std::endl;
    }
    // 1) Always execute the branch instruction itself:
    else if (inst.isBranch) {
        bool takeBranch = false;

        // 1) SPECIAL: compute‐unit dispatch
        if (inst.opcode == "beq" && inst.rs1 == 31) {
            // “beq x31, <targetCID>, label”
            if (coreId == inst.rs2) {
                takeBranch = true;           // only THIS compute-unit takes it
            } else {
                inst.shouldExecute = false;  // all the others skip it entirely
            }
        }
        // 2) Ordinary branches: beq/blt/bne/bge
        else {
            int op1 = getForwardedValue(inst.rs1);
            int op2 = getForwardedValue(inst.rs2);
            if      (inst.opcode == "beq") takeBranch = (op1 == op2);
            else if (inst.opcode == "bne") takeBranch = (op1 != op2);
            else if (inst.opcode == "blt") takeBranch = (op1 <  op2);
            else if (inst.opcode == "bge") takeBranch = (op1 >= op2);
        }

        // debugging
        std::cout << "[Core " << coreId
                  << "] Branch " << (takeBranch ? "taken" : "not taken") << "\n";
        std::cout << "    Clock cycle : " << cycleCount << std::endl;

        if (takeBranch) {
            // resolve label → targetPC
            if (inst.targetPC < 0 && !inst.label.empty()) {
                auto it = labels.find(inst.label);
                if (it != labels.end()) inst.targetPC = it->second;
            }
            // redirect fetch, flush IF/ID
            pc = inst.targetPC;
            fetchQueue.clear();
            decodeQueue.clear();
            executeQueue.clear();
                  memoryQueue.clear();
        }

        // only push into memory if shouldExecute still true
        if (inst.shouldExecute)
            memoryQueue.push_back(inst);

        return;
    }



    else if (inst.isJump) {
        if (inst.targetPC == -1 && !inst.label.empty()) {
            auto it = labels.find(inst.label);
            if (it != labels.end()) {
                inst.targetPC = it->second;
            }
            else {
                std::cerr << "[Core " << coreId << "] Error: label " << inst.label << " not found!\n";
            }
        }
        int returnAddr = executeJump(inst);
        if (returnAddr != -1) {
            inst.resultValue = returnAddr;
            inst.hasResult = true;
        } else {
            inst.hasResult = false; // no result to write
        }
        std::cout << "[Core " << coreId << "] Jump to PC: "
                << inst.targetPC << "\n";
        std::cout << "    Clock cycle : " << cycleCount << std::endl;
        pc = inst.targetPC;
        fetchQueue.clear();
        decodeQueue.clear();
        executeQueue.clear();
        memoryQueue.clear();
        memoryQueue.push_back(inst);
               recordStageForInstruction(inst.id, "E"); // or "M" depending where you record
                return;
    }
    else if (inst.opcode == "la") {
        if (!inst.label.empty()) {
            auto it = labels.find(inst.label);
            if (it != labels.end()) {
                inst.resultValue = it->second;
            }
            else {
                std::cerr << "[Core " << coreId << "] Error: label " << inst.label << " not found!\n";
                inst.resultValue = 0;
            }
        }
        else {
            inst.resultValue = 0;
        }
        inst.hasResult = true;
        std::cout << "[Core " << coreId << "] Loaded address: " << inst.resultValue
                << " into register x" << inst.rd << "\n";
        std::cout << "    Clock cycle : " << cycleCount << std::endl;
    }
    else if (inst.opcode == "invld1") {
        std::cout << "[Core " << coreId << "] Executing invld1: invalidating L1D cache for core " << coreId << "\n";
        memoryHierarchy->invalidateL1D(coreId);  // This must call the flush/invalidate method for your L1D cache
        recordStageForInstruction(inst.id, "E");
        memoryQueue.push_back(inst);  // Proceed to memory stage
        return;
    }


    if (memoryQueue.size() >= 2) {
        recordStageForInstruction(inst.id, "S");
        cycleStallOccurred = true;
        shouldStall = true;
        stallCount++;
        return;
    }

    memoryQueue.push_back(inst);
}

void PipelinedCore::memoryAccess(bool &shouldStall) {
    shouldStall = false;

    if (memoryQueue.empty()) {
        return;
    }

    Instruction inst = memoryQueue.front();
    
    // If this instruction is waiting for memory access, continue waiting
    if (inst.waitingForMemory) {
        if (inst.memoryLatency > 0) {
            recordStageForInstruction(inst.id, "S");
            inst.memoryLatency--;
            stallCount++;
            memoryQueue.front().memoryLatency = inst.memoryLatency;
            
            std::cout << "[Core " << coreId << "] Memory stage: Waiting for memory access, "
                      << inst.memoryLatency << " cycles remaining" << std::endl;
                      
            pipeline.incrementMemoryStallCycles(1);
            return;
        } else {
            // Memory access complete, continue processing
            memoryQueue.pop_front();
            std::cout << "[Core " << coreId << "] Memory stage: Loaded value: "
                      << inst.resultValue
                      << " from address " /* the same effectiveAddress */
                      << " (completion)" << std::endl;
            std::cout << "    Clock cycle : " << cycleCount << std::endl;

            // move to writeback, exactly as you do later
            writebackQueue.push_back(inst);
            recordStageForInstruction(inst.id, "M");

            return;
        }
    }
    
    memoryQueue.pop_front();

    if (inst.isMemory) {
        const int segmentSizeBytes = 1024;
        int segmentStart = coreId * segmentSizeBytes;
        int segmentEnd = (coreId + 1) * segmentSizeBytes - 4;

        if (inst.opcode == "lw") {
            if (memoryHierarchy) {
                int effectiveAddress = inst.resultValue;
                auto statsBefore = memoryHierarchy->getL1DCacheStats(coreId);
                std::cout << "[Debug Core " << coreId << "] L1D stats before load - "
                          << "Accesses=" << statsBefore.accesses
                          << ", Hits=" << statsBefore.hits
                          << ", Misses=" << statsBefore.misses << std::endl;
                // Access memory through cache hierarchy
                auto [latency, value] = memoryHierarchy->loadWord(coreId, effectiveAddress);
                inst.resultValue = value;
                inst.hasResult = true;
                auto statsAfter = memoryHierarchy->getL1DCacheStats(coreId);
                std::cout << "[Debug Core " << coreId << "] L1D stats after load - "
                          << "Accesses=" << statsAfter.accesses
                          << ", Hits=" << statsAfter.hits
                          << ", Misses=" << statsAfter.misses << std::endl;

                // Calculate the differences
                std::cout << "[Debug Core " << coreId << "] L1D accesses for this load: "
                          << (statsAfter.accesses - statsBefore.accesses) << std::endl;

                if (latency > 1) {
                    // If memory access takes more than one cycle, stall the pipeline
                    inst.memoryLatency = latency - 1;
                    inst.waitingForMemory = true;
                    recordStageForInstruction(inst.id, "S");
                    memoryQueue.push_front(inst);
                    stallCount++;
                    cycleStallOccurred = true;
                    shouldStall = true;
                    pipeline.incrementMemoryStallCycles(1);
                    return;
                }

                std::cout << "[Core " << coreId << "] Memory stage: Loaded value: "
                        << inst.resultValue << " from address " << effectiveAddress
                        << " (latency: " << latency << " cycles)" << std::endl;
            } else {
                // Legacy direct memory access
                int effectiveAddress = inst.resultValue;
                auto [latency, value] = memoryHierarchy->loadWord(coreId, effectiveAddress);
                inst.resultValue = value;
              //  inst.resultValue = sharedMemory->loadWord(coreId, effectiveAddress);
                inst.hasResult = true;
                std::cout << "[Core " << coreId << "] Memory stage: Loaded value: "
                        << inst.resultValue << " from address " << effectiveAddress << "\n";
            }
            std::cout << "    Clock cycle : " << cycleCount << std::endl;
        }
        else if (inst.opcode == "sw") {
            if (memoryHierarchy) {
                int effectiveAddress = inst.rs1;
                int valueToStore = inst.rs2;
                
                // Access memory through cache hierarchy
                int latency = memoryHierarchy->storeWord(coreId, effectiveAddress, valueToStore);
                
                if (latency > 1) {
                    // If memory access takes more than one cycle, stall the pipeline
                    inst.memoryLatency = latency - 1;
                    inst.waitingForMemory = true;
                    recordStageForInstruction(inst.id, "S");
                    memoryQueue.push_front(inst);
                    stallCount++;
                    cycleStallOccurred = true;
                    shouldStall = true;
                    pipeline.incrementMemoryStallCycles(1);
                    return;
                }
                
                std::cout << "[Core " << coreId << "] Memory stage: Stored value: "
                        << valueToStore << " to address " << effectiveAddress 
                        << " (latency: " << latency << " cycles)" << std::endl;
            } else {
                // Legacy direct memory access
                int effectiveAddress = inst.rs1;
                int valueToStore = inst.rs2;
                if (effectiveAddress >= segmentStart && effectiveAddress <= segmentEnd) {
                    memoryHierarchy->storeWord(coreId, effectiveAddress, valueToStore);
                    //sharedMemory->storeWord(coreId, effectiveAddress, valueToStore);
                    std::cout << "[Core " << coreId << "] Memory stage: Stored value: "
                            << valueToStore << " to address " << effectiveAddress << "\n";
                } else {
                    std::cout << "[Core " << coreId << "] Memory stage: Store address out of range: "
                            << effectiveAddress << "\n";
                }
            }
            std::cout << "    Clock cycle : " << cycleCount << std::endl;
        }
        else if (inst.opcode == "lw_spm") {
            if (memoryHierarchy) {
                int effectiveAddress = inst.resultValue;
                
                // Access scratchpad memory
                auto [latency, value] = memoryHierarchy->loadWordFromSPM(coreId, effectiveAddress);
                inst.resultValue = value;
                inst.hasResult = true;
                
                if (latency > 1) {
                    // If SPM access takes more than one cycle, stall the pipeline
                    inst.memoryLatency = latency - 1;
                    inst.waitingForMemory = true;
                    recordStageForInstruction(inst.id, "S");
                    memoryQueue.push_front(inst);
                    stallCount++;
                    cycleStallOccurred = true;
                    shouldStall = true;
                    return;
                }
                
                std::cout << "[Core " << coreId << "] Memory stage: Loaded value: "
                        << inst.resultValue << " from SPM address " << effectiveAddress 
                        << " (latency: " << latency << " cycles)" << std::endl;
            } else {
                // No SPM available, error
                std::cerr << "[Core " << coreId << "] Error: SPM not available" << std::endl;
                inst.resultValue = 0;
                inst.hasResult = true;
            }
        }
        else if (inst.opcode == "sw_spm") {
            if (memoryHierarchy) {
                int effectiveAddress = inst.rs1;
                int valueToStore = inst.rs2;
                
                // Access scratchpad memory
                int latency = memoryHierarchy->storeWordToSPM(coreId, effectiveAddress, valueToStore);
                
                if (latency > 1) {
                    // If SPM access takes more than one cycle, stall the pipeline
                    inst.memoryLatency = latency - 1;
                    inst.waitingForMemory = true;
                    recordStageForInstruction(inst.id, "S");
                    memoryQueue.push_front(inst);
                    stallCount++;
                    cycleStallOccurred = true;
                    shouldStall = true;
                    return;
                }
                
                std::cout << "[Core " << coreId << "] Memory stage: Stored value: "
                        << valueToStore << " to SPM address " << effectiveAddress 
                        << " (latency: " << latency << " cycles)" << std::endl;
            } else {
                // No SPM available, error
                std::cerr << "[Core " << coreId << "] Error: SPM not available" << std::endl;
            }
        }
    }

    if (writebackQueue.size() >= 2) {
        recordStageForInstruction(inst.id, "S");
        cycleStallOccurred = true;
        shouldStall = true;
        stallCount++;
        memoryQueue.push_front(inst);
        return;
    }

    writebackQueue.push_back(inst);
    recordStageForInstruction(inst.id, "M");
    // if (inst.isSync) {
    //     syncMechanism->retire(coreId);
    // }
    // if (inst.isSync) {
    //     syncMechanism->retire(coreId);
    //     std::atomic_thread_fence(std::memory_order_seq_cst);
    //     // std::cout << "[Barrier] core " << coreId
    //     //           << " retired (count="  /* retireCount */ << ")\n";
    //     // then push to writebackQueue or drop as a no-op
    //     writebackQueue.push_back(inst);
    //     recordStageForInstruction(inst.id, "M");
    //     return;
    // }

}

void PipelinedCore::writeback(bool &shouldStall) {
    shouldStall = false;

    if (writebackQueue.empty()) {
        return;
    }

    Instruction inst = writebackQueue.front();
    writebackQueue.pop_front();
    if (inst.opcode == "halt") {
        std::cout << "[Core " << coreId
                << "] Retiring HALT: flushing entire pipeline and stopping fetch\n";
        // 1) flush everything
        fetchQueue.clear();
        decodeQueue.clear();
        executeQueue.clear();
        memoryQueue.clear();
        writebackQueue.clear();
        // 2) record the retirement of HALT
        recordStageForInstruction(inst.id, "W");
        for (int c = 0; c < 4; ++c){

            memoryHierarchy->flushL1D(c);
    }
        memoryHierarchy->flushCache();

        halted = true;
        return;
    }
    if (!inst.shouldExecute) {
        return;
    }



    if (inst.hasResult && inst.rd >= 0) {
        if (inst.rd != 0 && inst.rd != 31) {
            if (pipeline.isForwardingEnabled()) {
                setRegister(inst.rd, inst.resultValue);
                registerAvailableCycle[inst.rd] = cycleCount + 1;
            }

            else {
                pendingWrites[inst.rd] = inst.resultValue;
            }
        }
    }

    instructionCount++;
    recordStageForInstruction(inst.id, "W");

    if (inst.isSync) {
        syncMechanism->retire(coreId);
        std::cout << "[Core " << coreId << "] Retire SYNC in WB\n";
    }
}

bool PipelinedCore::isPipelineEmpty() const {
    return fetchQueue.empty() && decodeQueue.empty() &&
           executeQueue.empty() && memoryQueue.empty() && writebackQueue.empty();
}

bool PipelinedCore::checkHaltCondition() {
    return (!writebackQueue.empty() && writebackQueue.front().opcode == "halt");
}

bool PipelinedCore::isHalted() const {
    return halted;
}

void PipelinedCore::clockCycle() {
    if (halted) return;
    cycleStallOccurred = false;

    for (auto &entry: pipelineRecord) {
        if (!entry.second.empty()) {
            if (entry.second.back() != "W") {
                if (entry.second.size() < static_cast<size_t>(cycleCount + 1)) {
                    entry.second.push_back("S");
                }
            }
        }
    }

    bool stallDecode = false, stallExecute = false, stallMemory = false, stallWriteback = false;
    writeback(stallWriteback);
    memoryAccess(stallMemory);
    execute(stallExecute);
    decode(stallDecode);

    cycleCount++;

    if (checkHaltCondition()) {
        halted = true;
    }

    if (!pipeline.isForwardingEnabled()) {
        std::cout << "[Debug] End of cycle " << cycleCount << ": updating pending writes" << std::endl;
        for (auto &entry: pendingWrites) {
            std::cout << "[Debug] Updating reg x" << entry.first << " to " << entry.second << std::endl;
            setRegister(entry.first, entry.second);
            registerAvailableCycle[entry.first] = cycleCount;
        }
        pendingWrites.clear();
    }
}

bool PipelinedCore::hasDataHazard(const Instruction &inst) const {
    if (inst.rs1 < 0 && inst.rs2 < 0) {
        return false;
    }

    for (const auto &execInst: executeQueue) {
        if (execInst.rd > 0 && (execInst.rd == inst.rs1 || execInst.rd == inst.rs2)) {
            if (!pipeline.isForwardingEnabled() || execInst.opcode == "lw") {
                return true;
            }
        }
    }

    return false;
}

bool PipelinedCore::hasControlHazard(const Instruction &inst) const {
    return inst.isBranch || inst.isJump;
}

bool PipelinedCore::canForwardData(const Instruction &consumer, int &rs1Value, int &rs2Value) const {
    rs1Value = getRegister(consumer.rs1);
    rs2Value = getRegister(consumer.rs2);
    bool rs1Found = false;
    bool rs2Found = false;

    for (const auto &inst: executeQueue) {
        if (inst.hasResult) {
            if (!rs1Found && inst.rd == consumer.rs1) {
                rs1Value = inst.resultValue;
                rs1Found = true;
            }
            if (!rs2Found && inst.rd == consumer.rs2) {
                rs2Value = inst.resultValue;
                rs2Found = true;
            }
        }
    }

    for (const auto &inst: memoryQueue) {
        if (inst.hasResult) {
            if (!rs1Found && inst.rd == consumer.rs1) {
                rs1Value = inst.resultValue;
                rs1Found = true;
            }
            if (!rs2Found && inst.rd == consumer.rs2) {
                rs2Value = inst.resultValue;
                rs2Found = true;
            }
        }
    }

    for (const auto &inst: writebackQueue) {
        if (inst.hasResult) {
            if (!rs1Found && inst.rd == consumer.rs1) {
                rs1Value = inst.resultValue;
                rs1Found = true;
            }
            if (!rs2Found && inst.rd == consumer.rs2) {
                rs2Value = inst.resultValue;
                rs2Found = true;
            }
        }
    }

    return rs1Found && rs2Found;
}

// int PipelinedCore::executeArithmetic(const Instruction &inst) {
//     if (inst.opcode == "add") {
//         return inst.rs1 + inst.rs2;
//     }
//     else if (inst.opcode == "addi") {
//         return inst.rs1 + inst.immediate;
//     }
//     else if (inst.opcode == "sub") {
//         return inst.rs1 - inst.rs2;
//     }
//     else if (inst.opcode == "slt") {
//         return (inst.rs1 < inst.rs2) ? 1 : 0;
//     }
//     else if (inst.opcode == "mul") {
//         return inst.rs1 * inst.rs2;
//     }
//     return 0;
// }
int PipelinedCore::executeArithmetic(int op1, int op2, int imm, const std::string &opcode) {
    if (opcode == "add") return op1 + op2;
    if (opcode == "sub") return op1 - op2;
    if (opcode == "slt") return (op1 < op2) ? 1 : 0;
    if (opcode == "mul") return op1 * op2;
    if (opcode == "addi") return op1 + imm;
    return 0;
}

bool PipelinedCore::executeBranch(const Instruction &inst) {
    if (inst.opcode == "bne") {

        return inst.rs1!= inst.rs2;
    }
    if (inst.opcode == "blt") {

        return inst.rs1 <inst.rs2 ;
    }
    return false;
}

int PipelinedCore::executeJump(const Instruction &inst) {
    if (inst.rd == 0) {
        return -1; // Indicate no return address
    }
    return pc + 1;
}

void PipelinedCore::setLabels(const std::unordered_map<std::string, int> &lbls) {
    labels = lbls;
}

const std::unordered_map<std::string, int> &PipelinedCore::getLabels() const {
    return labels;
}

double PipelinedCore::getIPC() const {
    if (cycleCount == 0) return 0.0;
    return static_cast<double>(instructionCount) / cycleCount;
}

// Add this method to your PipelinedCore class if it doesn't already exist:

