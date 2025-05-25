#include "pipelined_core.hpp"
#include <stdexcept>
#include <algorithm>
#include <iostream>
#include <unordered_set>
#include <fstream>
#include <iomanip>
#include <vector>
#include <unordered_map>

PipelinedCore::PipelinedCore(int id, std::shared_ptr<SharedMemory> memory, bool enableForwarding)
    : coreId(id)
      , registers(NUM_REGISTERS, 0)
      , sharedMemory(memory)
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

    for (auto &pair: const_cast<std::unordered_map<int, std::vector<std::string> > &>(pipelineRecord)) {
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

    fetchQueue.pop_front();

    if (inst.opcode == "beq") {
        int targetCID = inst.rs2;
        if (coreId != targetCID) {
            inst.shouldExecute = false;
        }
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

    if (inst.isArithmetic) {
        int op1 = 0, op2 = 0;
        if (fromDecode) {
            if (inst.opcode == "addi") {
                op1 = getForwardedValue(inst.rs1);
                op2 = inst.immediate;
            }
            else {
                op1 = getForwardedValue(inst.rs1);
                op2 = getForwardedValue(inst.rs2);
            }
            inst.rs1 = op1;
            inst.rs2 = op2;
        }
        else {
            op1 = inst.rs1;
            op2 = inst.rs2;
        }

        inst.resultValue = executeArithmetic(inst);
        inst.hasResult = true;
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
    else if (inst.isMemory && inst.opcode == "lw") {
        int base = getForwardedValue(inst.rs1);
        int effectiveAddress = base + inst.immediate;

        inst.resultValue = effectiveAddress;

        std::cout << "[Core " << coreId << "] Memory load address calculated: "
                << effectiveAddress << "\n";
        std::cout << "    Clock cycle : " << cycleCount << std::endl;
    }
    else if (inst.isMemory && inst.opcode == "sw") {
        int base = getForwardedValue(inst.rs1);
        int valueToStore = getForwardedValue(inst.rs2);

        int effectiveAddress = base + inst.immediate;

        inst.rs1 = effectiveAddress;
        inst.rs2 = valueToStore;

        std::cout << "[Core " << coreId << "] Memory store address calculated: "
                << effectiveAddress << ", Value to store: " << valueToStore << "\n";
        std::cout << "    Clock cycle : " << cycleCount << std::endl;
    }
    else if (inst.isBranch) {
        bool takeBranch = false;
        if (inst.opcode == "beq") {
            if (coreId == inst.rs2) {
                takeBranch = true;
            }
            else {
                inst.shouldExecute = false;
            }
        }
        else if (inst.opcode == "blt") {
            int operand1 = getForwardedValue(inst.rs1);
            int operand2 = getForwardedValue(inst.rs2);
            takeBranch = (operand1 < operand2);
        }
        else if (inst.opcode == "bne") {
            int operand1 = getForwardedValue(inst.rs1);
            int operand2 = getForwardedValue(inst.rs2);
            takeBranch = (operand1 != operand2);
        }
        std::cout << "[Core " << coreId << "] Branch " << (takeBranch ? "taken" : "not taken") << "\n";
        std::cout << "    Clock cycle : " << cycleCount << std::endl;
        if (takeBranch) {
            if (inst.targetPC == -1 && !inst.label.empty()) {
                auto it = labels.find(inst.label);
                if (it != labels.end()) {
                    inst.targetPC = it->second;
                }
                else {
                    std::cerr << "[Core " << coreId << "] Error: label " << inst.label << " not found!\n";
                }
            }
            pc = inst.targetPC;
            fetchQueue.clear();
            decodeQueue.clear();
        }
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
        inst.resultValue = returnAddr;
        inst.hasResult = true;
        std::cout << "[Core " << coreId << "] Jump to PC: "
                << inst.targetPC << "\n";
        std::cout << "    Clock cycle : " << cycleCount << std::endl;
        pc = inst.targetPC;
        fetchQueue.clear();
        decodeQueue.clear();
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
    memoryQueue.pop_front();

    if (inst.isMemory) {
        const int segmentSizeBytes = 1024;
        int segmentStart = coreId * segmentSizeBytes;
        int segmentEnd = (coreId + 1) * segmentSizeBytes - 4;

        if (inst.opcode == "lw") {
            int effectiveAddress = inst.resultValue;

            inst.resultValue = sharedMemory->loadWord(coreId, effectiveAddress);
            inst.hasResult = true;
            std::cout << "[Core " << coreId << "] Memory stage: Loaded value: "
                    << inst.resultValue << " from address " << effectiveAddress << "\n";
            std::cout << "    Clock cycle : " << cycleCount << std::endl;
        }
        else if (inst.opcode == "sw") {
            int effectiveAddress = inst.rs1;
            int valueToStore = inst.rs2;
            if (effectiveAddress >= segmentStart && effectiveAddress <= segmentEnd) {
                sharedMemory->storeWord(coreId, effectiveAddress, valueToStore);
                std::cout << "[Core " << coreId << "] Memory stage: Stored value: "
                        << valueToStore << " to address " << effectiveAddress << "\n";
                std::cout << "    Clock cycle : " << cycleCount << std::endl;
            }
            else {
                std::cout << "[Core " << coreId << "] Memory stage: Store address out of range: "
                        << effectiveAddress << "\n";
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
}

void PipelinedCore::writeback(bool &shouldStall) {
    shouldStall = false;

    if (writebackQueue.empty()) {
        return;
    }

    Instruction inst = writebackQueue.front();
    writebackQueue.pop_front();

    if (!inst.shouldExecute) {
        return;
    }

    if (inst.opcode == "halt") {
        halted = true;
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

int PipelinedCore::executeArithmetic(const Instruction &inst) {
    if (inst.opcode == "add") {
        return inst.rs1 + inst.rs2;
    }
    else if (inst.opcode == "addi") {
        return inst.rs1 + inst.immediate;
    }
    else if (inst.opcode == "sub") {
        return inst.rs1 - inst.rs2;
    }
    else if (inst.opcode == "slt") {
        return (inst.rs1 < inst.rs2) ? 1 : 0;
    }
    else if (inst.opcode == "mul") {
        return inst.rs1 * inst.rs2;
    }
    return 0;
}

// int PipelinedCore::executeMemoryLoad(const Instruction &inst) {
//     int effectiveAddress = inst.rs1 + inst.immediate;
//
//     const int segmentSizeBytes = 1024;
//     int segmentStart = coreId * segmentSizeBytes;
//     int segmentEnd = (coreId + 1) * segmentSizeBytes - 4;
//
//     if (effectiveAddress < segmentStart || effectiveAddress > segmentEnd) {
//         return 0;
//     }
//
//     return sharedMemory->loadWord(coreId, effectiveAddress);
// }
//
// void PipelinedCore::executeMemoryStore(const Instruction &inst) {
//     int effectiveAddress = inst.rs1 + inst.immediate;
//
//     const int segmentSizeBytes = 1024;
//     int segmentStart = coreId * segmentSizeBytes;
//     int segmentEnd = (coreId + 1) * segmentSizeBytes - 4;
//
//     if (effectiveAddress < segmentStart || effectiveAddress > segmentEnd) {
//         return;
//     }
//
//     sharedMemory->storeWord(coreId, effectiveAddress, inst.rs2);
// }

bool PipelinedCore::executeBranch(const Instruction &inst) {
    if (inst.opcode == "bne") {
        return inst.rs1 != inst.rs2;
    }
    else if (inst.opcode == "blt") {
        return inst.rs1 < inst.rs2;
    }
    return false;
}

int PipelinedCore::executeJump(const Instruction &inst) {
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
