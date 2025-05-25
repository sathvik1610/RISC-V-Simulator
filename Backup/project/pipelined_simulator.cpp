#include "pipelined_simulator.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iomanip>
#include <algorithm>
#include "centralized_fetch.hpp"
#include "sync_barrier.hpp"


PipelinedSimulator::PipelinedSimulator(int numCores,
                                       int l1Size, int l1BlockSize, int l1Assoc, int l1Latency,
                                       int l2Size, int l2BlockSize, int l2Assoc, int l2Latency,
                                       int memLatency, int spmSize, int spmLatency)
    : sharedMemory(std::make_shared<SharedMemory>()),
      scratchpad(std::make_shared<Scratchpad>(spmSize, spmLatency)),
      forwardingEnabled(true),
      syncBarrier(std::make_shared<SyncBarrier>(numCores))
{
    if (numCores <= 0 || numCores > 16)
        throw std::invalid_argument("Number of cores must be between 1 and 16");

    // Initialize memory hierarchy with fixed write-back/write-allocate policies
    memoryHierarchy = std::make_shared<MemoryHierarchy>(
        numCores,
        l1Size, l1BlockSize, l1Assoc, l1Latency,
        l2Size, l2BlockSize, l2Assoc, l2Latency,
        memLatency,
        sharedMemory,
        Cache::Policy::LRU,
        WritePolicy::WRITE_BACK,
        WriteAllocatePolicy::WRITE_ALLOCATE);

    // Initialize cache configurations
    CacheConfig l1Config = {
        l1Size, l1BlockSize, l1Assoc, l1Latency,
        Cache::Policy::LRU};

    CacheConfig l2Config = {
        l2Size, l2BlockSize, l2Assoc, l2Latency,
        Cache::Policy::LRU};

    SpmConfig spmConfig = {
        spmSize, spmLatency};
    SyncBarrier globalBarrier(4);
    // Create cores
    for (int i = 0; i < numCores; ++i)
    {
        cores.emplace_back(
            i, sharedMemory, forwardingEnabled,
            l1Config, l1Config, l2Config, spmConfig,
            memLatency, memoryHierarchy,&globalBarrier);
    }

    // Set up default instruction latencies
    instructionLatencies = {
        {"add", 1}, {"addi", 1}, {"sub", 1}, {"slt", 1}, {"mul", 3}, {"lw", 1}, {"sw", 1}, {"lw_spm", spmLatency}, {"sw_spm", spmLatency}};

    std::cerr << "[INFO] Simulator initialized with " << numCores << " cores\n";
    std::cerr << "[INFO] Cache configuration: L1=" << l1Size << "B, L2=" << l2Size << "B\n";
    std::cerr << "[INFO] Scratchpad size: " << spmSize << "B, latency: " << spmLatency << " cycles\n";
    std::cerr << "[INFO] Cache policies: Write-Back and Write-Allocate\n";
}

void PipelinedSimulator::setCachePolicy(Cache::Policy policy)
{
    memoryHierarchy->setCachePolicy(policy);
}

void PipelinedSimulator::setWritePolicy(WritePolicy policy)
{
    // Always maintain write-back, ignore the parameter
    memoryHierarchy->setWritePolicy(WritePolicy::WRITE_BACK);
}

void PipelinedSimulator::setWriteAllocatePolicy(WriteAllocatePolicy policy)
{
    // Always maintain write-allocate, ignore the parameter
    memoryHierarchy->setWriteAllocatePolicy(WriteAllocatePolicy::WRITE_ALLOCATE);
}

void PipelinedSimulator::loadProgramFromFile(const std::string &filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        throw std::runtime_error("Could not open file: " + filename);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    loadProgram(buffer.str());
}

void PipelinedSimulator::loadProgram(const std::string &assembly)
{
    std::istringstream iss(assembly);
    std::string line;
    program.clear();
    labelMap.clear();

    int dataPointer = 0;
    bool inDataSection = false;
    bool inTextSection = false;
    int programCounter = 0;

    std::string accumulatedData;

    auto flushData = [&]()
    {
        if (!accumulatedData.empty())
        {
            std::istringstream dataStream(accumulatedData);
            std::string token;
            while (std::getline(dataStream, token, ','))
            {
                token.erase(0, token.find_first_not_of(" \t\n\r"));
                token.erase(token.find_last_not_of(" \t\n\r") + 1);
                if (!token.empty())
                {
                    int value = std::stoi(token);
                    // Store directly in shared memory (no segmentation)
                    sharedMemory->setWord(dataPointer, value);
                    dataPointer += 4;
                }
            }
            std::cerr << "[DEBUG] Data flushed. Data pointer: " << dataPointer << "\n";
            accumulatedData = "";
        }
    };

    while (std::getline(iss, line))
    {
        // Remove comments
        size_t commentPos = line.find('#');
        if (commentPos != std::string::npos)
        {
            line = line.substr(0, commentPos);
        }

        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t\n\r"));
        line.erase(line.find_last_not_of(" \t\n\r") + 1);

        if (line.empty())
            continue;

        // Handle section directives
        if (line[0] == '.')
        {
            if (line.find(".data") != std::string::npos)
            {
                inDataSection = true;
                inTextSection = false;
                flushData();
                continue;
            }
            else if (line.find(".text") != std::string::npos)
            {
                flushData();
                inTextSection = true;
                inDataSection = false;
                continue;
            }
            else if (line.find(".globl") != std::string::npos)
            {
                continue;
            }
        }

        // Process data section
        if (inDataSection)
        {
            size_t colonPos = line.find(':');
            if (colonPos != std::string::npos)
            {
                flushData();

                std::string label = line.substr(0, colonPos);
                label.erase(0, label.find_first_not_of(" \t\n\r"));
                label.erase(label.find_last_not_of(" \t\n\r") + 1);
                if (!label.empty() && label[0] == '.')
                    label = label.substr(1);

                labelMap[label] = dataPointer;
                std::cerr << "[DEBUG] Data label loaded: '" << label
                          << "' at address " << dataPointer << "\n";

                std::string rest = line.substr(colonPos + 1);
                rest.erase(0, rest.find_first_not_of(" \t\n\r"));
                size_t pos = rest.find(".word");
                if (pos != std::string::npos)
                    rest = rest.substr(pos + 5);
                accumulatedData = rest;
            }
            else
            {
                if (!accumulatedData.empty())
                    accumulatedData = accumulatedData + "," + line;
                else
                    accumulatedData = line;
            }
        }
        // Process text section
        else if (inTextSection)
        {
            size_t colonPos = line.find(':');
            if (colonPos != std::string::npos)
            {
                std::string label = line.substr(0, colonPos);
                label.erase(0, label.find_first_not_of(" \t\n\r"));
                label.erase(label.find_last_not_of(" \t\n\r") + 1);
                labelMap[label] = programCounter;
                std::cerr << "[DEBUG] Text label loaded: '" << label
                          << "' at instruction " << programCounter << "\n";

                std::string rest = line.substr(colonPos + 1);
                rest.erase(0, rest.find_first_not_of(" \t\n\r"));
                if (!rest.empty())
                {
                    program.push_back(rest);
                    programCounter++;
                }
            }
            else
            {
                program.push_back(line);
                programCounter++;
            }
        }
    }

    // Flush any remaining data
    if (inDataSection && !accumulatedData.empty())
    {
        flushData();
    }

    std::cerr << "[DEBUG] Loaded program with " << program.size() << " instructions\n";
    std::cerr << "[DEBUG] Loaded " << labelMap.size() << " labels\n";

    // Initialize cores with the program
    for (auto &core : cores)
    {
        core.reset();
        core.setLabels(labelMap);

        // Set instruction latencies for each core
        for (const auto &[instruction, latency] : instructionLatencies)
        {
            core.setInstructionLatency(instruction, latency);
        }
    }
}

void PipelinedSimulator::setForwardingEnabled(bool enabled)
{
    forwardingEnabled = enabled;
    for (auto &core : cores)
    {
        core.setForwardingEnabled(enabled);
    }
}

bool PipelinedSimulator::isForwardingEnabled() const
{
    return forwardingEnabled;
}

void PipelinedSimulator::setInstructionLatency(const std::string &instruction, int latency)
{
    if (latency < 1)
    {
        throw std::invalid_argument("Instruction latency must be at least 1");
    }

    instructionLatencies[instruction] = latency;

    for (auto &core : cores)
    {
        core.setInstructionLatency(instruction, latency);
    }
}

int PipelinedSimulator::getInstructionLatency(const std::string &instruction) const
{
    auto it = instructionLatencies.find(instruction);
    if (it != instructionLatencies.end())
    {
        return it->second;
    }
    return 1; // Default latency
}

void PipelinedSimulator::run()
{
    // Initialize tracking for halted cores
    std::vector<bool> coreHalted(cores.size(), false);

    // Reset counters and statistics
    memoryHierarchy->resetStats();
    for (auto &core : cores)
    {
        core.resetCacheStats();
    }

    int simulationCycle = 0;
    std::cerr << "[INFO] Starting simulation with " << cores.size() << " cores\n";

    // Main simulation loop
    while (true)
    {
        // Increment cycle counters
        memoryHierarchy->incrementCycle();

        // Debug output
        std::cerr << "[DEBUG] Cycle " << simulationCycle
                  << ": Centralizing fetch across cores\n";

        // Fetch instructions for all cores
        centralizedFetch(cores, program, memoryHierarchy);

        // Debug output for core status
        std::cerr << "[DEBUG] Core statuses after fetch:\n";
        for (size_t coreId = 0; coreId < cores.size(); coreId++)
        {
            std::cerr << "  Core " << coreId
                      << " - Halted: " << (cores[coreId].isHalted() ? "true" : "false")
                      << ", Pipeline Empty: " << (cores[coreId].isPipelineEmpty() ? "true" : "false")
                      << ", PC: " << cores[coreId].getPC()
                      << ", Fetch Queue Size: " << cores[coreId].getFetchQueueSize() << "\n";
        }

        // Execute one clock cycle for each core
        bool allCoresHalted = true;
        for (size_t coreId = 0; coreId < cores.size(); coreId++)
        {
            std::cerr << "[DEBUG] Processing core " << coreId << " for cycle " << simulationCycle << "\n";

            // Run one cycle for this core
            cores[coreId].clockCycle();

            // Check if core is halted
            if (cores[coreId].isHalted() ||
                (cores[coreId].getPC() >= program.size() && cores[coreId].isPipelineEmpty()))
            {
                coreHalted[coreId] = true;
            }
            else
            {
                allCoresHalted = false;
            }

            std::cerr << "[DEBUG] Core " << coreId << " processed. Halted: "
                      << (coreHalted[coreId] ? "true" : "false") << "\n";
        }

        // Increment simulation cycle counter
        simulationCycle++;

        // Exit simulation when all cores are halted
        if (allCoresHalted)
        {
            std::cerr << "[INFO] All cores halted after " << simulationCycle << " cycles\n";
            break;
        }
    }

    // Output final state and statistics
    std::cerr << "[INFO] Simulation complete\n";
    printState();
    printStatistics();
}

bool PipelinedSimulator::isExecutionComplete() const
{
    for (const auto &core : cores)
    {
        if (core.getPC() < program.size() || !core.isPipelineEmpty())
        {
            return false;
        }
    }
    return true;
}

void PipelinedSimulator::printState() const
{
    std::cout << "\n=== Final Simulator State ===\n";

    // Print state for each core
    for (const auto &core : cores)
    {
        std::cout << "\n=== Core " << core.getCoreId() << " State ===\n";
        std::cout << "PC: 0x" << std::hex << std::setw(8) << std::setfill('0')
                  << core.getPC() << std::dec << "\n\n";

        // Print register values
        std::cout << "Registers:\n";
        const auto &registers = core.getRegisters();
        for (size_t i = 0; i < registers.size(); i++)
        {
            std::cout << "x" << std::setw(2) << std::setfill('0') << i
                      << ": 0x" << std::hex << std::setw(8) << std::setfill('0')
                      << registers[i] << std::dec;
            if (i == 0)
                std::cout << " (zero)";
            if (i == 31)
                std::cout << " (core_id)";
            std::cout << "\n";
        }

        // Export pipeline visualization
        core.exportPipelineRecord("pipeline_core" + std::to_string(core.getCoreId()) + ".csv");
    }

    // Dump memory contents
    const int memoryDumpSize = 256; // Bytes to dump from memory
    std::cout << "\n=== Memory Dump (first " << memoryDumpSize << " bytes) ===\n";

    for (int addr = 0; addr < memoryDumpSize; addr += 16)
    {
        std::cout << std::hex << std::setw(8) << std::setfill('0') << addr << ": ";

        for (int offset = 0; offset < 16; offset += 4)
        {
            if (addr + offset < SharedMemory::TOTAL_MEMORY_SIZE)
            {
                int value = sharedMemory->loadWord(0, addr + offset);
                std::cout << std::hex << std::setw(8) << std::setfill('0') << value << " ";
            }
        }
        std::cout << "\n";
    }

    // Dump scratchpad memory
    std::cout << "\n=== Scratchpad Memory Dump ===\n";
    scratchpad->dump();

    std::cout << std::dec; // Reset to decimal output
}

void PipelinedSimulator::printStatistics() const
{
    std::cout << "\n=== Pipeline Statistics ===\n";

    unsigned long totalCycles = 0;
    unsigned long totalInstructions = 0;
    unsigned long totalStalls = 0;
    unsigned long totalCacheStalls = 0;

    // Print per-core statistics
    for (const auto &core : cores)
    {
        auto coreCycles = core.getCycleCount();
        auto coreInsns = core.getInstructionCount();
        auto coreStalls = core.getStallCount();
        auto coreCacheStalls = core.getCacheStallCount();

        std::cout << "Core " << core.getCoreId() << ":\n"
                  << "  Instructions executed: " << coreInsns << "\n"
                  << "  Cycles:                " << coreCycles << "\n"
                  << "  Pipeline stalls:       " << coreStalls << "\n"
                  << "  Cache stalls:          " << coreCacheStalls << "\n"
                  << "  IPC:                   "
                  << std::fixed << std::setprecision(2)
                  << core.getIPC() << "\n\n";

        totalCycles = std::max(totalCycles, coreCycles);
        totalInstructions += coreInsns;
        totalStalls += coreStalls;
        totalCacheStalls += coreCacheStalls;
    }

    // Print overall statistics
    std::cout << "Overall Statistics:\n"
              << "  Total instructions: " << totalInstructions << "\n"
              << "  Total cycles:       " << totalCycles << "\n"
              << "  Total stalls:       " << totalStalls << "\n"
              << "  Total cache stalls: " << totalCacheStalls << "\n"
              << "  Overall IPC:        "
              << std::fixed << std::setprecision(2)
              << (totalCycles > 0 ? double(totalInstructions) / totalCycles : 0.0)
              << "\n\n";

    // Print detailed cache statistics
    std::cout << "Cache Statistics:\n";

    // Table header
    std::cout << std::left << std::setw(6) << "Core"
              << " | " << std::setw(15) << "Cache"
              << " | " << std::setw(10) << "Accesses"
              << " | " << std::setw(10) << "Hits"
              << " | " << std::setw(10) << "Misses"
              << " | " << std::setw(12) << "Miss Rate"
              << "\n";
    std::cout << std::string(75, '-') << "\n";

    // Per-core L1I and L1D
    for (const auto &core : cores)
    {
        int id = core.getCoreId();

        // L1I
        std::cout << std::left << std::setw(6) << id
                  << " | " << std::setw(15) << "L1I Cache"
                  << " | " << std::setw(10) << core.getL1IAccessCount()
                  << " | " << std::setw(10) << core.getL1IHitCount()
                  << " | " << std::setw(10) << core.getL1IMissCount()
                  << " | " << std::setw(12) << std::fixed << std::setprecision(4)
                  << core.getL1IMissRate() << "\n";

        // L1D
        std::cout << std::left << std::setw(6) << id
                  << " | " << std::setw(15) << "L1D Cache"
                  << " | " << std::setw(10) << core.getL1DAccessCount()
                  << " | " << std::setw(10) << (core.getL1DHitCount())
                  << " | " << std::setw(10) << core.getL1DMissCount()
                  << " | " << std::setw(12) << std::fixed << std::setprecision(4)
                  << core.getL1DMissRate() << "\n";
    }

    // Shared L2
    std::cout << std::left << std::setw(6) << "All"
              << " | " << std::setw(15) << "L2 Cache"
              << " | " << std::setw(10) << memoryHierarchy->getL2AccessCount()
              << " | " << std::setw(10) << memoryHierarchy->getL2HitCount()
              << " | " << std::setw(10) << memoryHierarchy->getL2MissCount()
              << " | " << std::setw(12) << std::fixed << std::setprecision(4)
              << memoryHierarchy->getL2MissRate() << "\n";

    // Main memory
    std::cout << std::left << std::setw(6) << "All"
              << " | " << std::setw(15) << "Main Memory"
              << " | " << std::setw(10) << memoryHierarchy->getMemAccessCount()
              << " | " << std::setw(10) << "0"
              << " | " << std::setw(10) << memoryHierarchy->getMemAccessCount()
              << " | " << std::setw(12) << "1.0000" << "\n\n";

    // Cache policies & config
    std::cout << "Cache Policies:\n"
              << "  Write Policy:          Write-Back\n"
              << "  Write Allocate Policy: Write-Allocate\n"
              << "  Replacement Policy:    "
              << (cores[0].getL1ICache()->getReplacementPolicy() == Cache::Policy::LRU
                      ? "LRU\n"
                      : "FIFO\n");

    std::cout << "Pipeline Configuration:\n"
              << "  Forwarding: "
              << (isForwardingEnabled() ? "Enabled\n" : "Disabled\n");

    std::cout << "Instruction Latencies:\n";
    for (const auto &p : instructionLatencies)
    {
        std::cout << "  " << p.first << ": " << p.second << " cycle(s)\n";
    }
}
