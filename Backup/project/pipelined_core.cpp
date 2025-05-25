#include "pipelined_core.hpp"
#include "memory_hierarchy.hpp"
#include <stdexcept>
#include <algorithm>
#include <iostream>
#include <unordered_set>
#include <fstream>
#include <iomanip>
#include <vector>
#include <unordered_map>
#include <atomic>


int PipelinedCore::totalCores = 0;

PipelinedCore::PipelinedCore(int id,
                             std::shared_ptr<SharedMemory> memory,
                             bool enableForwarding,
                             CacheConfig l1i_cfg, CacheConfig l1d_cfg,
                             CacheConfig l2_cfg, SpmConfig spm_cfg,
                             int memLatency, std::shared_ptr<MemoryHierarchy> memHierarchy,SyncBarrier* b)
    : coreId(id),
      registers(NUM_REGISTERS, 0),
      sharedMemory(memory),
      pc(0),
      pipeline(enableForwarding),
      cycleCount(0),
      stallCount(0),
      cacheStallCount(0),
      instructionCount(0),
      halted(false),
      cycleStallOccurred(false),
      mainMemoryLatency(memLatency),
      memoryHierarchy(memHierarchy),
      fetchCounter(0),
          barrier(b)
{
    registers[31] = coreId; // CID register

    // Initialize caches
    l1ICache = std::make_shared<Cache>(
        l1i_cfg.size, l1i_cfg.blockSize,
        l1i_cfg.associativity, l1i_cfg.latency,
        l1i_cfg.replacementPolicy);

    l1DCache = std::make_shared<Cache>(
        l1d_cfg.size, l1d_cfg.blockSize,
        l1d_cfg.associativity, l1d_cfg.latency,
        l1d_cfg.replacementPolicy);

    // l2Cache = std::make_shared<Cache>(
    //     l2_cfg.size, l2_cfg.blockSize,
    //     l2_cfg.associativity, l2_cfg.latency,
    //     l2_cfg.replacementPolicy);

    // Initialize scratchpad memory
    spm = std::make_shared<Scratchpad>(spm_cfg.size, spm_cfg.latency);

    totalCores++;
}

void PipelinedCore::reset()
{
    std::fill(registers.begin(), registers.end(), 0);
    registers[31] = coreId; // Restore CID register
    pc = 0;
    labels.clear();

    fetchQueue.clear();
    decodeQueue.clear();
    executeQueue.clear();
    memoryQueue.clear();
    writebackQueue.clear();
    pendingWrites.clear();
    registerAvailableCycle.clear();

    cycleCount = 0;
    stallCount = 0;
    cacheStallCount = 0;
    instructionCount = 0;
    halted = false;
    cycleStallOccurred = false;

    pipeline.reset();
    fetchCounter = 0;
    pipelineRecord.clear();
}

int PipelinedCore::getForwardedValue(int reg) const
{
    if (reg == 0)
        return 0;
    if (reg == 31)
        return coreId;

    if (!pipeline.isForwardingEnabled())
    {
        return getRegister(reg);
    }

    // Check for forwarded values in the pipeline stages
    for (const auto &inst : writebackQueue)
    {
        if (inst.hasResult && inst.rd == reg)
            return inst.resultValue;
    }

    for (const auto &inst : memoryQueue)
    {
        if (inst.hasResult && inst.rd == reg)
            return inst.resultValue;
    }

    for (const auto &inst : executeQueue)
    {
        if (inst.hasResult && inst.rd == reg)
            return inst.resultValue;
    }

    // If no forwarded value found, return the register value
    return getRegister(reg);
}

void PipelinedCore::exportPipelineRecord(const std::string &filename) const
{
    std::ofstream outFile(filename);
    if (!outFile.is_open())
    {
        std::cerr << "Error opening file " << filename << std::endl;
        return;
    }

    // Write CSV header
    outFile << "InstrID";
    for (int cycle = 0; cycle <= cycleCount; cycle++)
    {
        outFile << ",Cycle" << (cycle + 1);
    }
    outFile << "\n";

    // Extract and sort instruction IDs
    std::vector<int> keys;
    for (const auto &pair : pipelineRecord)
    {
        keys.push_back(pair.first);
    }
    std::sort(keys.begin(), keys.end());

    // Create a mapping from original IDs to sequential IDs for readability
    std::unordered_map<int, int> idMapping;
    int normalizedId = 1;
    for (int id : keys)
    {
        idMapping[id] = normalizedId++;
    }

    // Ensure all records have the same length
    for (auto &pair : const_cast<std::unordered_map<int, std::vector<std::string>> &>(pipelineRecord))
    {
        while (pair.second.size() < static_cast<size_t>(cycleCount))
        {
            pair.second.push_back("");
        }
    }

    // Write pipeline record for each instruction
    for (int id : keys)
    {
        outFile << idMapping[id];
        auto it = pipelineRecord.find(id);
        if (it != pipelineRecord.end())
        {
            for (const auto &stage : it->second)
            {
                outFile << "," << stage;
            }
        }
        outFile << "\n";
    }

    outFile.close();
    std::cout << "Pipeline record exported to " << filename << std::endl;
}

bool PipelinedCore::isPipelineStalled() const
{
    // Check if a stall occurred in the current cycle
    if (cycleStallOccurred)
        return true;

    // Check if any stage queue is full
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

void PipelinedCore::recordStageForInstruction(int instId, const std::string &stage)
{
    // Initialize record for this instruction if it doesn't exist
    if (pipelineRecord.find(instId) == pipelineRecord.end())
    {
        pipelineRecord[instId] = std::vector<std::string>(cycleCount, "");
    }

    // Add the current stage to the record
    pipelineRecord[instId].push_back(stage);
}

int PipelinedCore::getRegister(int index) const
{
    if (index < 0 || index >= NUM_REGISTERS)
    {
        throw std::out_of_range("Register index out of range");
    }

    // Special registers
    if (index == 31)
    {
        return coreId; // CID register
    }
    if (index == 0)
    {
        return 0; // Zero register
    }

    return registers[index];
}

void PipelinedCore::setRegister(int index, int value)
{
    if (index < 0 || index >= NUM_REGISTERS)
    {
        throw std::out_of_range("Register index out of range");
    }

    // Cannot modify x0 (zero) or x31 (CID)
    if (index != 0 && index != 31)
    {
        registers[index] = value;
    }
}

void PipelinedCore::decode(bool &shouldStall)
{
    std::cerr << "[DEBUG] Core " << coreId << " decode() invoked. FetchQ="
              << fetchQueue.size() << "\n";
    shouldStall = false;

    // Nothing to decode if fetch queue is empty
    if (fetchQueue.empty())
    {
        return;
    }

    // If a stall has already occurred in this cycle, propagate it
    if (cycleStallOccurred)
    {
        shouldStall = true;
        return;
    }

    // Check if decode queue is full
    if (decodeQueue.size() >= 2)
    {
        const FetchEntry &entry = fetchQueue.front();
        recordStageForInstruction(entry.fetchId, "S");
        shouldStall = true;
        cycleStallOccurred = true;
        stallCount++;
        return;
    }

    // Get instruction from fetch queue
    auto entry = fetchQueue.front();

    // Skip label entries (should be handled by program loader)
    if (entry.rawInst.find(':') != std::string::npos)
    {
        incrementPC();
        fetchQueue.pop_front();
        return;
    }

    // Parse the instruction
    Instruction inst = InstructionParser::parseInstruction(entry.rawInst, coreId);
    if (inst.opcode == "sync") {
             inst.isSync = true;
               std::cerr << "[DEBUG] Core " << coreId
                         << " decoded SYNC inst with id=" << inst.id << "\n";
           }
    // Handle HALT instruction
    if (inst.isHalt)
    {
        std::cerr << "[DEBUG] Core " << coreId << " saw HALT in decode()\n";
        halted = true;
        fetchQueue.pop_front();
        return;
    }

    // Check for data hazards if forwarding is disabled
    if (!pipeline.isForwardingEnabled() && !operandsReadyForUse(inst))
    {
        recordStageForInstruction(entry.fetchId, "S");
        shouldStall = true;
        cycleStallOccurred = true;
        stallCount++;
        return;
    }

    // Assign instruction ID from fetch entry
    inst.id = entry.fetchId;
    fetchQueue.pop_front();

    // Special handling for BEQ with CID
    if (inst.opcode == "beq")
    {
        int targetCID = inst.rs2;
        if (coreId != targetCID)
        {
            inst.shouldExecute = false;
        }
    }

    // Set execution latency based on instruction type
    if (inst.isArithmetic)
    {
        inst.executeLatency = pipeline.getInstructionLatency(inst.opcode);
    }

    // Move instruction to decode queue
    decodeQueue.push_back(inst);
    recordStageForInstruction(inst.id, "D");
}

bool PipelinedCore::isRegisterInUse(int reg) const
{
    if (reg == 0)
    {
        return false; // Zero register is never "in use"
    }

    // Check if register is being modified in any pipeline stage
    for (const auto &inst : decodeQueue)
    {
        if (inst.rd == reg && inst.shouldExecute)
        {
            return true;
        }
    }

    for (const auto &inst : executeQueue)
    {
        if (inst.rd == reg && inst.shouldExecute)
        {
            return true;
        }
    }

    for (const auto &inst : memoryQueue)
    {
        if (inst.rd == reg && inst.shouldExecute)
        {
            return true;
        }
    }

    for (const auto &inst : writebackQueue)
    {
        if (inst.rd == reg && inst.shouldExecute)
        {
            return true;
        }
    }

    return false;
}

bool PipelinedCore::operandsReadyForUse(const Instruction &inst) const
{
    // Check both source registers
    for (int reg : {inst.rs1, inst.rs2})
    {
        if (reg == 0)
            continue; // x0 is always ready

        // Check if register is waiting to be written
        auto it = registerAvailableCycle.find(reg);
        if (it != registerAvailableCycle.end())
        {
            if (cycleCount < it->second)
            {
                return false; // Register is not ready yet
            }
        }
    }
    return true;
}

bool PipelinedCore::operandsAvailable(const Instruction &consumer) const
{
    if (!pipeline.isForwardingEnabled())
    {
        // Without forwarding, check if operands are pending writes
        if (pendingWrites.find(consumer.rs1) != pendingWrites.end() ||
            pendingWrites.find(consumer.rs2) != pendingWrites.end())
        {
            return false;
        }

        // Check all pipeline stages for register conflicts
        auto checkQueue = [this, &consumer](const std::deque<Instruction> &q, const std::string &qName) -> bool
        {
            for (const auto &inst : q)
            {
                if (inst.id == consumer.id)
                    continue; // Skip self
                if (inst.rd <= 0)
                    continue; // Skip if no destination register
                if ((consumer.rs1 != 0 && inst.rd == consumer.rs1) ||
                    (consumer.rs2 != 0 && inst.rd == consumer.rs2))
                {
                    std::cout << "  BLOCKING: inst id=" << inst.id
                              << " (rd=" << inst.rd << ") in " << qName
                              << " blocks consumer (id=" << consumer.id << ")" << std::endl;
                    return false;
                }
            }
            return true;
        };

        // Check each pipeline stage
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
    else
    {
        // With forwarding, check if operands have computed results available
        auto checkQueueForward = [this, &consumer](const std::deque<Instruction> &q, const std::string &qName) -> bool
        {
            for (const auto &inst : q)
            {
                if (inst.id == consumer.id)
                    continue; // Skip self
                if (inst.rd <= 0)
                    continue; // Skip if no destination register
                if (!inst.hasResult && ((consumer.rs1 != 0 && inst.rd == consumer.rs1) ||
                                        (consumer.rs2 != 0 && inst.rd == consumer.rs2)))
                {
                    std::cout << "  BLOCKING: inst id=" << inst.id
                              << " (rd=" << inst.rd << ") in " << qName
                              << " blocks consumer (id=" << consumer.id << ")" << std::endl;
                    return false;
                }
            }
            return true;
        };

        // Check each pipeline stage
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

void PipelinedCore::execute(bool &shouldStall)
{
    shouldStall = false;
    Instruction inst;
    bool fromDecode = false;

    // Try to get instruction from execute queue first
    if (!executeQueue.empty())
    {
        inst = executeQueue.front();
        executeQueue.pop_front();
    }
    // Otherwise, try to get instruction from decode queue
    else if (!decodeQueue.empty())
    {
        // Check for data hazards if forwarding is disabled
        if (!pipeline.isForwardingEnabled() && !operandsAvailable(decodeQueue.front()))
        {
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
    else
    {
        return; // No instruction to execute
    }

    // Record that this instruction is in execute stage
    recordStageForInstruction(inst.id, "E");

    std::cout << "[Core " << coreId << "] Executing instruction: "
              << inst.opcode << " (rs1: " << inst.rs1
              << ", rs2: " << inst.rs2 << ", rd: " << inst.rd << ")\n";
    std::cout << "   Clock cycle : " << cycleCount << std::endl;

    // Skip execution if instruction should not be executed (e.g., from branch)
    if (!inst.shouldExecute)
    {
        std::cout << "[Core " << coreId << "] Skipping instruction (shouldExecute = false)\n";
        memoryQueue.push_back(inst);
        return;
    }

    // Handle arithmetic instructions
    if (inst.isArithmetic)
    {
        int op1 = 0, op2 = 0;

        // Get operands depending on source (decode or execute queue)
        if (fromDecode)
        {
            if (inst.opcode == "addi")
            {
                op1 = getForwardedValue(inst.rs1);
                op2 = inst.immediate;
            }
            else
            {
                op1 = getForwardedValue(inst.rs1);
                op2 = getForwardedValue(inst.rs2);
            }
            inst.rs1 = op1;
            inst.rs2 = op2;
        }
        else
        {
            op1 = inst.rs1;
            op2 = inst.rs2;
        }

        // Execute the arithmetic operation and get result
        inst.resultValue = executeArithmetic(inst);
        inst.hasResult = true;

        std::cout << "[Core " << coreId << "] Arithmetic result: "
                  << inst.resultValue << "\n";
        std::cout << "    Clock cycle : " << cycleCount << std::endl;

        // Handle multi-cycle execution
        if (inst.executeLatency > 1)
        {
            inst.cyclesInExecute++;
            if (inst.cyclesInExecute < inst.executeLatency)
            {
                std::cout << "[Core " << coreId << "] Multi-cycle arithmetic: cycle "
                          << inst.cyclesInExecute << " of " << inst.executeLatency << "\n";
                executeQueue.push_back(inst);
                stallCount++;
                shouldStall = true;
                return;
            }
        }
    }
    // Handle load instruction
    else if (inst.isMemory && inst.opcode == "lw")
    {
        int base = getForwardedValue(inst.rs1);
        int effectiveAddress = base + inst.immediate;

        inst.resultValue = effectiveAddress; // Store address for memory stage

        std::cout << "[Core " << coreId << "] Memory load address calculated: "
                  << effectiveAddress << "\n";
        std::cout << "    Clock cycle : " << cycleCount << std::endl;
    }
    // Handle store instruction
    else if (inst.isMemory && inst.opcode == "sw")
    {
        int base = getForwardedValue(inst.rs1);
        int valueToStore = getForwardedValue(inst.rs2);

        int effectiveAddress = base + inst.immediate;

        inst.rs1 = effectiveAddress; // Store address
        inst.rs2 = valueToStore;     // Store value

        std::cout << "[Core " << coreId << "] Memory store address calculated: "
                  << effectiveAddress << ", Value to store: " << valueToStore << "\n";
        std::cout << "    Clock cycle : " << cycleCount << std::endl;
    }
    // Handle scratchpad memory load instruction
    else if (inst.isMemory && inst.opcode == "lw_spm")
    {
        int base = getForwardedValue(inst.rs1);
        int effectiveAddress = base + inst.immediate;

        inst.resultValue = effectiveAddress; // Store address for memory stage

        std::cout << "[Core " << coreId << "] SPM load address calculated: "
                  << effectiveAddress << "\n";
        std::cout << "    Clock cycle : " << cycleCount << std::endl;
    }
    // Handle scratchpad memory store instruction
    else if (inst.isMemory && inst.opcode == "sw_spm")
    {
        int base = getForwardedValue(inst.rs1);
        int valueToStore = getForwardedValue(inst.rs2);

        int effectiveAddress = base + inst.immediate;

        inst.rs1 = effectiveAddress; // Store address
        inst.rs2 = valueToStore;     // Store value

        std::cout << "[Core " << coreId << "] SPM store address calculated: "
                  << effectiveAddress << ", Value to store: " << valueToStore << "\n";
        std::cout << "    Clock cycle : " << cycleCount << std::endl;
    }
    // Handle branch instructions
    else if (inst.isBranch)
    {
        bool takeBranch = false;

        if (inst.opcode == "beq")
        {
            // Special handling for CID-dependent branches
            if (coreId == inst.rs2)
            {
                takeBranch = true;
            }
            else
            {
                inst.shouldExecute = false;
            }
        }
        else if (inst.opcode == "blt")
        {
            int operand1 = getForwardedValue(inst.rs1);
            int operand2 = getForwardedValue(inst.rs2);
            takeBranch = (operand1 < operand2);
        }
        else if (inst.opcode == "bne")
        {
            int operand1 = getForwardedValue(inst.rs1);
            int operand2 = getForwardedValue(inst.rs2);
            takeBranch = (operand1 != operand2);
        }
        else if (inst.opcode == "bge")
        {
            int operand1 = getForwardedValue(inst.rs1);
            int operand2 = getForwardedValue(inst.rs2);
            takeBranch = (operand1 >= operand2);
        }

        std::cout << "[Core " << coreId << "] Branch " << (takeBranch ? "taken" : "not taken") << "\n";
        std::cout << "    Clock cycle : " << cycleCount << std::endl;

        if (takeBranch)
        {
            // Resolve label to target PC
            if (inst.targetPC == -1 && !inst.label.empty())
            {
                auto it = labels.find(inst.label);
                if (it != labels.end())
                {
                    inst.targetPC = it->second;
                }
                else
                {
                    std::cerr << "[Core " << coreId << "] Error: label " << inst.label << " not found!\n";
                }
            }

            // Flush pipeline and update PC
            pc = inst.targetPC;
            fetchQueue.clear();
            decodeQueue.clear();
        }
    }
    // Handle jump instructions
    else if (inst.isJump)
    {
        // Resolve label to target PC
        if (inst.targetPC == -1 && !inst.label.empty())
        {
            auto it = labels.find(inst.label);
            if (it != labels.end())
            {
                inst.targetPC = it->second;
            }
            else
            {
                std::cerr << "[Core " << coreId << "] Error: label " << inst.label << " not found!\n";
            }
        }

        // For JAL, save return address
        int returnAddr = executeJump(inst);
        inst.resultValue = returnAddr;
        inst.hasResult = true;

        std::cout << "[Core " << coreId << "] Jump to PC: "
                  << inst.targetPC << "\n";
        std::cout << "    Clock cycle : " << cycleCount << std::endl;

        // Flush pipeline and update PC
        pc = inst.targetPC;
        fetchQueue.clear();
        decodeQueue.clear();
    }
    // Handle load address instruction
    else if (inst.opcode == "la")
    {
        if (!inst.label.empty())
        {
            auto it = labels.find(inst.label);
            if (it != labels.end())
            {
                inst.resultValue = it->second;
            }
            else
            {
                std::cerr << "[Core " << coreId << "] Error: label " << inst.label << " not found!\n";
                inst.resultValue = 0;
            }
        }
        else
        {
            inst.resultValue = 0;
        }

        inst.hasResult = true;

        std::cout << "[Core " << coreId << "] Loaded address: " << inst.resultValue
                  << " into register x" << inst.rd << "\n";
        std::cout << "    Clock cycle : " << cycleCount << std::endl;
    }

    // Check if memory queue is full, stall if necessary
    if (memoryQueue.size() >= 2)
    {
        recordStageForInstruction(inst.id, "S");
        cycleStallOccurred = true;
        shouldStall = true;
        stallCount++;
        return;
    }

    // Move instruction to memory queue
    memoryQueue.push_back(inst);
}

void PipelinedCore::memoryAccess(bool &shouldStall)
{
    shouldStall = false;
    if (memoryQueue.empty())
        return;

    Instruction inst = memoryQueue.front();
    memoryQueue.pop_front();

    // Handle SYNC instruction
    if (inst.isSync) {
        std::cout << "[Core " << coreId << "] ENTERING NEW SYNC handler" << std::endl;
        // 1) Arrive at the shared barrier
        int waitCycles = barrier->sync(coreId);

        // 2) Log the arrival
        std::cout << "[Core " << coreId << "] SYNC arrived" << std::endl;

        // 3) Trace the stage
        recordStageForInstruction(inst.id, "M");

        // 4) If not everyone’s here yet, stall this core for one cycle
        if (waitCycles > 0) {
            shouldStall = true;
            stallCount++;
            cycleStallOccurred = true;

            // Keep the SYNC instruction in the memory queue so
            // we retry it next cycle.
            memoryQueue.push_front(inst);
            return;
        }

        // 5) waitCycles == 0 ⇒ this was the last core,
        //    barrier is released now for *all* cores.
        std::cout << "[Core " << coreId << "] SYNC complete, advancing" << std::endl;
    }
    writebackQueue.push_back(inst);
    recordStageForInstruction(inst.id, "M");
    return;


    if (inst.isMemory)
    {
        int effectiveAddress = registers[inst.rs1] + inst.immediate;

        // Scratchpad memory operations
        if (inst.isSPM)
        {
            if (inst.opcode == "lw_spm")
            {
                // Load from scratchpad
                inst.resultValue = spm->load(effectiveAddress);
                inst.hasResult = true;

                // Apply scratchpad latency
                int latencyPenalty = spm->getAccessLatency() - 1;
                if (latencyPenalty > 0)
                {
                    stallCount += latencyPenalty;
                }

                std::cout << "[Core " << coreId << "] SPM load: " << inst.resultValue
                          << " from address " << effectiveAddress << " (latency: "
                          << spm->getAccessLatency() << ")\n";
            }
            else if (inst.opcode == "sw_spm")
            {
                // Store to scratchpad
                spm->store(effectiveAddress, inst.rs2);

                // Apply scratchpad latency
                int latencyPenalty = spm->getAccessLatency() - 1;
                if (latencyPenalty > 0)
                {
                    stallCount += latencyPenalty;
                }

                std::cout << "[Core " << coreId << "] SPM store: " << inst.rs2
                          << " to address " << effectiveAddress << " (latency: "
                          << spm->getAccessLatency() << ")\n";
            }
        }
        // Regular memory operations (through cache hierarchy)
        else
        {
            incrementL1DAccess();

            if (inst.opcode == "lw")
            {
                // Load from memory
                auto result = memoryHierarchy->accessData(
                    coreId,
                    effectiveAddress,
                    false); // Not a store
                if (result.hit)
                    incrementL1DHit();
                else
                    incrementL1DMiss();

                // Apply cache miss penalty if needed
                if (!result.hit)
                {
                    int penalty = result.latency - l1DCache->getAccessLatency();
                    if (penalty > 0)
                    {
                        cacheStallCount += penalty; // Add to cacheStallCount instead of stallCount
                        stallCount += penalty;
                        recordStageForInstruction(inst.id, "S");
                    }
                }

                // Store loaded value for writeback
                inst.resultValue = result.value;
                inst.hasResult = true;

                std::cout << "[Core " << coreId << "] Memory load: " << result.value
                          << " from address " << effectiveAddress
                          << " (" << (result.hit ? "hit" : "miss") << ", latency: "
                          << result.latency << ")\n";
            }
            else if (inst.opcode == "sw")
            {
                // Store to memory
                auto result = memoryHierarchy->accessData(
                    coreId,
                    effectiveAddress,
                    true,      // Is a store
                    inst.rs2); // Value to store
                if (result.hit)
                    incrementL1DHit();
                else
                    incrementL1DMiss();

                // Apply cache miss penalty if needed
                if (!result.hit)
                {
                    int penalty = result.latency - l1DCache->getAccessLatency();
                    if (penalty > 0)
                    {
                        cacheStallCount += penalty; // Add to cacheStallCount instead of stallCount
                        stallCount += penalty;
                        recordStageForInstruction(inst.id, "S");
                    }
                }

                std::cout << "[Core " << coreId << "] Memory store: " << inst.rs2
                          << " to address " << effectiveAddress
                          << " (" << (result.hit ? "hit" : "miss") << ", latency: "
                          << result.latency << ")\n";
            }
        }
    }

    // Check if writeback queue is full, stall if necessary
    if (writebackQueue.size() >= 2)
    {
        recordStageForInstruction(inst.id, "S");
        shouldStall = true;
        stallCount++;
        memoryQueue.push_front(inst);
        return;
    }

    // Move instruction to writeback queue
    writebackQueue.push_back(inst);
    recordStageForInstruction(inst.id, "M");
}

void PipelinedCore::writeback(bool &shouldStall)
{
    shouldStall = false;

    // Nothing to writeback if queue is empty
    if (writebackQueue.empty())
    {
        return;
    }

    // Get instruction from writeback queue
    Instruction inst = writebackQueue.front();
    writebackQueue.pop_front();

    // Skip writeback if instruction should not be executed
    if (!inst.shouldExecute)
    {
        return;
    }

    // Handle HALT instruction
    if (inst.isHalt)
    {
        halted = true;
        return;
    }

    // Write result to register if needed
    if (inst.hasResult && inst.rd >= 0)
    {
        if (inst.rd != 0 && inst.rd != 31) // Cannot write to x0 or x31
        {
            if (pipeline.isForwardingEnabled())
            {
                // With forwarding, update register directly
                setRegister(inst.rd, inst.resultValue);
                registerAvailableCycle[inst.rd] = cycleCount + 1;
            }
            else
            {
                // Without forwarding, queue the write for end of cycle
                pendingWrites[inst.rd] = inst.resultValue;
            }
        }
    }

    // Increment instruction count
    instructionCount++;

    // Record writeback stage completion
    recordStageForInstruction(inst.id, "W");
}

bool PipelinedCore::isPipelineEmpty() const
{
    return fetchQueue.empty() && decodeQueue.empty() &&
           executeQueue.empty() && memoryQueue.empty() && writebackQueue.empty();
}

bool PipelinedCore::checkHaltCondition()
{
    return (!writebackQueue.empty() && writebackQueue.front().isHalt);
}

bool PipelinedCore::isHalted() const
{
    return halted;
}

void PipelinedCore::clockCycle()
{
    std::cerr << "[DEBUG] Core " << getCoreId() << " clockCycle() invoked. PC=" << getPC()
              << ", PipelineEmpty=" << isPipelineEmpty()
              << ", Halted=" << (isHalted() ? "true" : "false") << "\n";

    // Skip execution if core is halted
    if (halted)
    {
        std::cerr << "[DEBUG] Core " << coreId << " is halted. Skipping stages.\n";
        return;
    }

    std::cerr << "[DEBUG] Core " << coreId << " proceeding to pipeline stages...\n";
    cycleStallOccurred = false;

    // Pad pipeline record for stalled instructions
    for (auto &entry : pipelineRecord)
    {
        if (!entry.second.empty())
        {
            if (entry.second.back() != "W")
            {
                if (entry.second.size() < static_cast<size_t>(cycleCount + 1))
                {
                    entry.second.push_back("S");
                }
            }
        }
    }

    // Execute pipeline stages in reverse order to prevent data hazards
    bool stallDecode = false, stallExecute = false, stallMemory = false, stallWriteback = false;
    writeback(stallWriteback);
    memoryAccess(stallMemory);
    execute(stallExecute);
    decode(stallDecode);

    // Increment cycle counter
    cycleCount++;

    // Check for halt instruction
    if (checkHaltCondition())
    {
        halted = true;
    }

    // Without forwarding, apply all pending writes at end of cycle
    if (!pipeline.isForwardingEnabled())
    {
        std::cout << "[Debug] End of cycle " << cycleCount << ": updating pending writes" << std::endl;
        for (auto &entry : pendingWrites)
        {
            std::cout << "[Debug] Updating reg x" << entry.first << " to " << entry.second << std::endl;
            setRegister(entry.first, entry.second);
            registerAvailableCycle[entry.first] = cycleCount;
        }
        pendingWrites.clear();
    }
}

bool PipelinedCore::hasDataHazard(const Instruction &inst) const
{
    if (inst.rs1 < 0 && inst.rs2 < 0)
    {
        return false; // No source registers
    }

    // Check for hazards in execute stage
    for (const auto &execInst : executeQueue)
    {
        if (execInst.rd > 0 && (execInst.rd == inst.rs1 || execInst.rd == inst.rs2))
        {
            // With forwarding, only loads cause hazards that require stalls
            if (!pipeline.isForwardingEnabled() || execInst.opcode == "lw")
            {
                return true;
            }
        }
    }

    return false;
}

bool PipelinedCore::hasControlHazard(const Instruction &inst) const
{
    return inst.isBranch || inst.isJump;
}

bool PipelinedCore::canForwardData(const Instruction &consumer, int &rs1Value, int &rs2Value) const
{
    rs1Value = getRegister(consumer.rs1);
    rs2Value = getRegister(consumer.rs2);
    bool rs1Found = false;
    bool rs2Found = false;

    // Check for forwarded values in each pipeline stage
    for (const auto &inst : executeQueue)
    {
        if (inst.hasResult)
        {
            if (!rs1Found && inst.rd == consumer.rs1)
            {
                rs1Value = inst.resultValue;
                rs1Found = true;
            }
            if (!rs2Found && inst.rd == consumer.rs2)
            {
                rs2Value = inst.resultValue;
                rs2Found = true;
            }
        }
    }

    for (const auto &inst : memoryQueue)
    {
        if (inst.hasResult)
        {
            if (!rs1Found && inst.rd == consumer.rs1)
            {
                rs1Value = inst.resultValue;
                rs1Found = true;
            }
            if (!rs2Found && inst.rd == consumer.rs2)
            {
                rs2Value = inst.resultValue;
                rs2Found = true;
            }
        }
    }

    for (const auto &inst : writebackQueue)
    {
        if (inst.hasResult)
        {
            if (!rs1Found && inst.rd == consumer.rs1)
            {
                rs1Value = inst.resultValue;
                rs1Found = true;
            }
            if (!rs2Found && inst.rd == consumer.rs2)
            {
                rs2Value = inst.resultValue;
                rs2Found = true;
            }
        }
    }

    return rs1Found && rs2Found;
}

int PipelinedCore::executeArithmetic(const Instruction &inst)
{
    if (inst.opcode == "add")
    {
        return inst.rs1 + inst.rs2;
    }
    else if (inst.opcode == "addi")
    {
        return inst.rs1 + inst.immediate;
    }
    else if (inst.opcode == "sub")
    {
        return inst.rs1 - inst.rs2;
    }
    else if (inst.opcode == "slt")
    {
        return (inst.rs1 < inst.rs2) ? 1 : 0;
    }
    else if (inst.opcode == "mul")
    {
        return inst.rs1 * inst.rs2;
    }

    // Unknown arithmetic operation
    std::cerr << "[ERROR] Unknown arithmetic operation: " << inst.opcode << std::endl;
    return 0;
}

bool PipelinedCore::executeBranch(const Instruction &inst)
{
    if (inst.opcode == "bne")
    {
        return inst.rs1 != inst.rs2;
    }
    else if (inst.opcode == "blt")
    {
        return inst.rs1 < inst.rs2;
    }
    else if (inst.opcode == "beq")
    {
        return inst.rs1 == inst.rs2;
    }
    else if (inst.opcode == "bge")
    {
        return inst.rs1 >= inst.rs2;
    }

    // Unknown branch operation
    std::cerr << "[ERROR] Unknown branch operation: " << inst.opcode << std::endl;
    return false;
}

int PipelinedCore::executeJump(const Instruction &inst)
{
    // Return address is PC + 1 for JAL
    return pc + 1;
}

void PipelinedCore::setLabels(const std::unordered_map<std::string, int> &lbls)
{
    labels = lbls;
}

const std::unordered_map<std::string, int> &PipelinedCore::getLabels() const
{
    return labels;
}

double PipelinedCore::getIPC() const
{
    if (cycleCount == 0)
        return 0.0;
    return static_cast<double>(instructionCount) / cycleCount;
}

double PipelinedCore::getL1ICacheMissRate() const
{
    return l1ICache->calculateMissRate();
}

double PipelinedCore::getL1DCacheMissRate() const
{
    return l1DCache->calculateMissRate();
}

double PipelinedCore::getL2CacheMissRate() const {
    return memoryHierarchy->getL2MissRate();
}

void PipelinedCore::resetCacheStats()
{
    l1ICache->resetStats();
    l1DCache->resetStats();
    // l2Cache->resetStats();
    l1i_accesses = l1i_hits = l1i_misses = 0;
    l1d_accesses = l1d_hits = l1d_misses = 0;
}