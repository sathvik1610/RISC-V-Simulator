#include "pipelined_simulator.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iomanip>
#include <algorithm>
#include "centralized_fetch.hpp"
#include "pipelined_core.hpp"

PipelinedSimulator::PipelinedSimulator(int numCores, bool enableForwarding)
    : sharedMemory(std::make_shared<SharedMemory>())
      , forwardingEnabled(enableForwarding) {
    if (numCores <= 0 || numCores > 16) {
        throw std::invalid_argument("Number of cores must be between 1 and 16");
    }

    for (int i = 0; i < numCores; i++) {
        cores.emplace_back(i, sharedMemory, forwardingEnabled);
    }

    instructionLatencies["add"] = 1;
    instructionLatencies["addi"] = 1;
    instructionLatencies["sub"] = 1;
    instructionLatencies["slt"] = 1;
    instructionLatencies["mul"] = 3;
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
                    for (int coreId = 0; coreId < cores.size(); coreId++) {
                        int baseAddress = coreId * SharedMemory::SEGMENT_SIZE;
                        sharedMemory->setWord(baseAddress + dataPointer, value);
                    }
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

    while (true) {
        bool allCoresHalted = true;

        centralizedFetch(cores, program);

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

        if (allCoresHalted)
            break;
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

    const int segmentSizeBytes = 1024;
    std::cout << "\n=== Memory Dump per Core ===\n";
    for (size_t coreId = 0; coreId < cores.size(); coreId++) {
        std::vector<int> segment = sharedMemory->getMemorySegment(coreId);
        int startAddr = coreId * segmentSizeBytes;
        int endAddr = startAddr + segmentSizeBytes - 1;

        std::cout << "\n--- Core " << coreId << " Memory (addresses "
                << std::hex << std::setw(8) << std::setfill('0') << startAddr << " to "
                << std::hex << std::setw(8) << std::setfill('0') << endAddr
                << ") ---\n" << std::dec;

        for (size_t i = 0; i < segment.size(); i++) {
            int address = startAddr + i * 4;
            if (i % 4 == 0) {
                std::cout << std::hex << std::setw(8) << std::setfill('0') << address << " 4 words" << ": ";
            }
            std::cout << std::hex << std::setw(8) << std::setfill('0') << segment[i] << " ";
            if (i % 4 == 3 || i == segment.size() - 1) {
                std::cout << "\n";
            }
        }
    }
    std::cout << std::dec;
}

void PipelinedSimulator::printStatistics() const {
    std::cout << "\n=== Pipeline Statistics ===\n";

    unsigned long totalCycles = 0;
    unsigned long totalInstructions = 0;
    unsigned long totalStalls = 0;

    for (const auto &core: cores) {
        unsigned long coreCycles = core.getCycleCount();
        unsigned long coreInstructions = core.getInstructionCount();
        unsigned long coreStalls = core.getStallCount();

        std::cout << "Core " << core.getCoreId() << ":\n";
        std::cout << "  Instructions executed: " << coreInstructions << "\n";
        std::cout << "  Cycles: " << coreCycles << "\n";
        std::cout << "  Stalls: " << coreStalls << "\n";
        std::cout << "  IPC: " << std::fixed << std::setprecision(2) << core.getIPC() << "\n\n";

        totalCycles = std::max(totalCycles, coreCycles);
        totalInstructions += coreInstructions;
        totalStalls += coreStalls;
    }

    std::cout << "Overall Statistics:\n";
    std::cout << "  Total instructions: " << totalInstructions << "\n";
    std::cout << "  Total cycles: " << totalCycles << "\n";
    std::cout << "  Total stalls: " << totalStalls << "\n";
    std::cout << "  Overall IPC: " << std::fixed << std::setprecision(2)
            << (totalCycles > 0 ? static_cast<double>(totalInstructions) / totalCycles : 0.0) << "\n";

    std::cout << "\nForwarding: " << (isForwardingEnabled() ? "Enabled" : "Disabled") << "\n";

    std::cout << "Instruction Latencies:\n";
    for (const auto &[instruction, latency]: instructionLatencies) {
        std::cout << "  " << instruction << ": " << latency << " cycle(s)\n";
    }
}
