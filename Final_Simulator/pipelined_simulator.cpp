
#include "pipelined_simulator.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iomanip>
#include <algorithm>
#include "centralized_fetch.hpp"
#include "pipelined_core.hpp"
#include "memory_hierarchy.hpp"

PipelinedSimulator::PipelinedSimulator(int numCores, bool enableForwarding)
    :
    forwardingEnabled(enableForwarding) {
    if (numCores <= 0 || numCores > 16) {
        throw std::invalid_argument("Number of cores must be between 1 and 16");
    }
    try {
        memoryHierarchy = std::make_shared<MemoryHierarchy>(numCores, "cache_config.txt");
    } catch (const std::exception& e) {
        std::cerr << "Error initializing memory hierarchy: " << e.what()
                  << "\nFalling back to direct memory access.\n";
        memoryHierarchy = nullptr;
    }
    syncMechanism = std::make_shared<SyncMechanism>(
        numCores,
        memoryHierarchy ? memoryHierarchy.get() : nullptr
    );

    // 3) Create cores and wire them up
    for (int i = 0; i < numCores; ++i) {
        cores.emplace_back(i, forwardingEnabled);
        if (memoryHierarchy)
            cores.back().setMemoryHierarchy(memoryHierarchy);
        cores.back().setSyncMechanism(syncMechanism);
    }



    // Create sync mechanism
    //syncMechanism = std::make_shared<SyncMechanism>(numCores);

    // Create cores
    // for (int i = 0; i < numCores; i++) {
    //     cores.emplace_back(i,forwardingEnabled);
    // }

    instructionLatencies["add"] = 1;
    instructionLatencies["addi"] = 1;
    instructionLatencies["sub"] = 1;
    instructionLatencies["slt"] = 1;
    instructionLatencies["mul"] = 1;

    // Load default cache configuration
    // try {
    //     memoryHierarchy = std::make_shared<MemoryHierarchy>(numCores, "cache_config.txt");
    //
    //     // Connect memory hierarchy to cores
    //     for (auto& core : cores) {
    //         core.setMemoryHierarchy(memoryHierarchy);
    //         core.setSyncMechanism(syncMechanism);
    //     }
    // } catch (const std::exception& e) {
    //     std::cerr << "Error initializing memory hierarchy: " << e.what() << std::endl;
    //     std::cerr << "Continuing with direct memory access" << std::endl;
    // }
}

void PipelinedSimulator::loadProgramFromFile(const std::string &filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + filename);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    loadProgram(buffer.str());
}

void PipelinedSimulator::loadProgram(const std::string& assembly) {
    std::istringstream iss(assembly);
    std::string line;
    program.clear();
    labelMap.clear();

    int dataPointer = 0;
    bool inDataSection = false;
    bool inTextSection = true;
    int programCounter = 0;


    std::string accumulatedData;


    auto flushData = [&]() {
        if (!accumulatedData.empty()) {
            std::istringstream dataStream(accumulatedData);
            std::string token;
            while (std::getline(dataStream, token, ',')) {
                token.erase(0, token.find_first_not_of(" \t\n\r"));
                token.erase(token.find_last_not_of(" \t\n\r") + 1);
                if (!token.empty()) {
                    int value = std::stoi(token);
                    // Store data to the shared memory at a single location
                    // All cores can access the same memory location
                   // sharedMemory->setWord(dataPointer, value);
                    memoryHierarchy->getMainMemory()->setWord(dataPointer, value);
                    dataPointer += 4;
                }
            }
            accumulatedData = "";
        }
    };

    while (std::getline(iss, line)) {

        size_t commentPos = line.find('#');
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
        }


        line.erase(0, line.find_first_not_of(" \t\n\r"));
        line.erase(line.find_last_not_of(" \t\n\r") + 1);

        if (line.empty())
            continue;


        if (line[0] == '.') {
            if (line.find(".data") != std::string::npos) {
                inDataSection = true;
                inTextSection = false;
                flushData();
                continue;
            } else if (line.find(".text") != std::string::npos) {
                flushData();
                inTextSection = true;
                inDataSection = false;
                continue;
            } else if (line.find(".globl") != std::string::npos) {
                continue;
            }
        }

        if (inDataSection) {
            size_t colonPos = line.find(':');
            if (colonPos != std::string::npos) {

                flushData();


                std::string label = line.substr(0, colonPos);
                label.erase(0, label.find_first_not_of(" \t\n\r"));
                label.erase(label.find_last_not_of(" \t\n\r") + 1);
                if (!label.empty() && label[0] == '.')
                    label = label.substr(1);

                labelMap[label] = dataPointer;


                std::string rest = line.substr(colonPos + 1);
                rest.erase(0, rest.find_first_not_of(" \t\n\r"));
                size_t pos = rest.find(".word");
                if (pos != std::string::npos)
                    rest = rest.substr(pos + 5);
                accumulatedData = rest;
            } else {

                if (!accumulatedData.empty())
                    accumulatedData = accumulatedData + "," + line;
                else
                    accumulatedData = line;
            }
        } else if (inTextSection) {
            size_t colonPos = line.find(':');
            if (colonPos != std::string::npos) {
                std::string label = line.substr(0, colonPos);
                label.erase(0, label.find_first_not_of(" \t\n\r"));
                label.erase(label.find_last_not_of(" \t\n\r") + 1);
                labelMap[label] = programCounter;

                std::string rest = line.substr(colonPos + 1);
                rest.erase(0, rest.find_first_not_of(" \t\n\r"));
                if (!rest.empty()) {
                    program.push_back(rest);
                    programCounter++;
                }
            } else {
                program.push_back(line);
                programCounter++;
            }
        }
    }


    if (inDataSection && !accumulatedData.empty()) {
        flushData();
    }


    for (auto& core : cores) {
        core.reset();
        core.setLabels(labelMap);
        for (const auto& [instruction, latency] : instructionLatencies) {
            core.setInstructionLatency(instruction, latency);
        }
    }

    // Reset the sync mechanism
    if (syncMechanism) {
        syncMechanism->reset();
    }
}

void PipelinedSimulator::loadCacheConfig(const std::string& filename) {
    try {
        memoryHierarchy = std::make_shared<MemoryHierarchy>(cores.size(), filename);

        // Connect memory hierarchy to cores
        for (auto& core : cores) {
            core.setMemoryHierarchy(memoryHierarchy);
        }

        std::cout << "Cache configuration loaded from " << filename << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error loading cache configuration: " << e.what() << std::endl;
        std::cerr << "Continuing with previous or default configuration" << std::endl;
    }
}

void PipelinedSimulator::setForwardingEnabled(bool enabled) {
    forwardingEnabled = enabled;
    for (auto &core: cores) {
        core.setForwardingEnabled(enabled);
    }
}

bool PipelinedSimulator::isForwardingEnabled() const {
    return forwardingEnabled;
}

void PipelinedSimulator::setInstructionLatency(const std::string &instruction, int latency) {
    if (latency < 1) {
        throw std::invalid_argument("Instruction latency must be at least 1");
    }

    instructionLatencies[instruction] = latency;

    for (auto &core: cores) {
        core.setInstructionLatency(instruction, latency);
    }
}

int PipelinedSimulator::getInstructionLatency(const std::string &instruction) const {
    auto it = instructionLatencies.find(instruction);
    if (it != instructionLatencies.end()) {
        return it->second;
    }
    return 1;
}

void PipelinedSimulator::run() {
    std::vector<bool> coreHalted(cores.size(), false);
    std::vector<bool>  coreDraining(cores.size(), false);

    if (memoryHierarchy) {
        memoryHierarchy->resetStatistics();
    }
    centralizedFetch(cores, program);

    while (true) {
        bool allCoresHalted = true;

       // centralizedFetch(cores, program);

        for (size_t coreId = 0; coreId < cores.size(); coreId++) {
            if (!coreHalted[coreId]) {
                cores[coreId].clockCycle();

                if (cores[coreId].isHalted() || cores[coreId].isPipelineEmpty()) {
                    coreHalted[coreId] = true;
                }
                else {
                    allCoresHalted = false;
                }
            }
        }
       centralizedFetch(cores, program);

        if (allCoresHalted)
            break;
    }
    // for (size_t i = 0; i < cores.size(); i++) {
    //     memoryHierarchy->getL1D(i)->flushCache();    // ensure write-back of every dirty line
    // }
    if (memoryHierarchy) {
               memoryHierarchy->flushCache();
            }

    printState();
    printStatistics();
}

bool PipelinedSimulator::isExecutionComplete() const {
    for (const auto &core: cores) {
        if (core.getPC() < program.size() || !core.isPipelineEmpty()) {
            return false;
        }
    }
    return true;
}

void PipelinedSimulator::printState() const {
    std::cout << "\n=== Final Simulator State ===\n";

    for (const auto &core: cores) {
        std::cout << "\n=== Core " << core.getCoreId() << " State ===\n";
        std::cout << "PC: 0x" << std::hex << std::setw(8) << std::setfill('0')
                << core.getPC() << std::dec << "\n\n";

        std::cout << "Registers:\n";
        const auto &registers = core.getRegisters();
        for (size_t i = 0; i < registers.size(); i++) {
            std::cout << "x" << std::setw(2) << std::setfill('0') << i
                    << ": 0x" << std::hex << std::setw(8) << std::setfill('0')
                    << registers[i] << std::dec;
            if (i == 0) std::cout << " (zero)";
            if (i == 31) std::cout << " (core_id)";
            std::cout << "\n";
        }
        core.exportPipelineRecord("pipeline_core" + std::to_string(core.getCoreId()) + ".csv");
    }

    const auto &bytes = memoryHierarchy->getRawMemory();
    size_t numWords = bytes.size() / 4;

    std::cout << "\n=== Complete Shared Memory Dump ===\n";
    std::cout << "All cores have access to the entire memory space\n";

    for (size_t w = 0; w < numWords; w++) {
        // Compute the byte address of this word
        uint32_t addr = static_cast<uint32_t>(w * 4);

        // Reconstruct a little‑endian 32‑bit word from 4 bytes
        int32_t word = 0;
        for (int b = 0; b < 4; b++) {
            word |= (static_cast<int32_t>(bytes[w*4 + b]) << (8 * b));
        }

        // Print 4 words per line
        if (w % 4 == 0) {
            std::cout << std::hex << std::setw(8) << std::setfill('0')
                      << addr << ": ";
        }
        std::cout << std::hex << std::setw(8) << std::setfill('0')
                  << static_cast<uint32_t>(word) << " ";
        if (w % 4 == 3 || w == numWords - 1) {
            std::cout << "\n";
        }
    }
    std::cout << std::dec;
}

void PipelinedSimulator::printStatistics() const {
    std::cout << "\n=== Pipeline Statistics ===\n";

    unsigned long totalCycles = 0;
    unsigned long totalInstructions = 0;
    unsigned long totalStalls = 0;
    unsigned long totalMemoryStalls = 0;

    for (const auto &core: cores) {
        unsigned long coreCycles = core.getCycleCount();
        unsigned long coreInstructions = core.getInstructionCount();
        unsigned long coreStalls = core.getStallCount();
        unsigned long coreMemoryStalls = core.getMemoryStallCycles();

        std::cout << "Core " << core.getCoreId() << ":\n";
        std::cout << "  Instructions executed: " << coreInstructions << "\n";
        std::cout << "  Cycles: " << coreCycles << "\n";
        std::cout << "  Total stalls: " << coreStalls << "\n";
        std::cout << "  Memory stalls: " << coreMemoryStalls << "\n";
        std::cout << "  IPC: " << std::fixed << std::setprecision(2) << core.getIPC() << "\n\n";

        totalCycles = std::max(totalCycles, coreCycles);
        totalInstructions += coreInstructions;
        totalStalls += coreStalls;
        totalMemoryStalls += coreMemoryStalls;
    }

    std::cout << "Overall Statistics:\n";
    std::cout << "  Total instructions: " << totalInstructions << "\n";
    std::cout << "  Total cycles: " << totalCycles << "\n";
    std::cout << "  Total stalls: " << totalStalls << "\n";
    std::cout << "  Memory stalls: " << totalMemoryStalls << " (" 
              << std::fixed << std::setprecision(1)
              << (totalStalls > 0 ? (totalMemoryStalls * 100.0 / totalStalls) : 0.0)
              << "% of all stalls)\n";
    std::cout << "  Overall IPC: " << std::fixed << std::setprecision(2)
            << (totalCycles > 0 ? static_cast<double>(totalInstructions) / totalCycles : 0.0) << "\n";

    std::cout << "\nForwarding: " << (isForwardingEnabled() ? "Enabled" : "Disabled") << "\n";

    std::cout << "Instruction Latencies:\n";
    for (const auto &[instruction, latency]: instructionLatencies) {
        std::cout << "  " << instruction << ": " << latency << " cycle(s)\n";
    }
    
    // Print cache statistics if available
    if (memoryHierarchy) {
        memoryHierarchy->printStatistics();
    }
}