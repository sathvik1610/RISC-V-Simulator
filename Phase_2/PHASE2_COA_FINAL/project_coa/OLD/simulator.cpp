#include "simulator.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <iomanip>
#include <unordered_map>

RISCVSimulator::RISCVSimulator(int numCores)
    : sharedMemory(std::make_shared<SharedMemory>())
    , barrierCount(0)
    , running(true)
{
    if (numCores <= 0 || numCores > 16) {
        throw std::invalid_argument("Number of cores must be between 1 and 16");
    }
    for (int i = 0; i < numCores; i++) {
        cores.emplace_back(i, sharedMemory);
    }
}

void RISCVSimulator::loadProgramFromFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + filename);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    loadProgram(buffer.str());
}

void RISCVSimulator::loadProgram(const std::string &assembly)
{
    std::istringstream iss(assembly);
    std::string line;
    program.clear();
    std::unordered_map<std::string, int> labelMap;

    int dataPointer = 0;
    bool inDataSection = false;
    bool inTextSection = true;

    while (std::getline(iss, line))
    {
        std::string processedLine = trimAndRemoveComments(line);
        if (processedLine.empty())
            continue;

        if (processedLine[0] == '.')
        {
            if (processedLine.find(".data") != std::string::npos)
            {
                inDataSection = true;
                inTextSection = false;
                continue;
            }
            else if (processedLine.find(".text") != std::string::npos)
            {
                inTextSection = true;
                inDataSection = false;
                continue;
            }
            else if (processedLine.find(".globl") != std::string::npos)
            {
                continue;
            }
        }

        if (inDataSection)
        {
            size_t colonPos = processedLine.find(':');
            if (colonPos != std::string::npos)
            {
                std::string label = trim(processedLine.substr(0, colonPos));
                labelMap[label] = dataPointer;
                std::string rest = trim(processedLine.substr(colonPos + 1));
                if (rest.find(".word") != std::string::npos)
                {
                    size_t pos = rest.find(".word");
                    std::string numbersStr = rest.substr(pos + 5);
                    std::istringstream numStream(numbersStr);
                    std::string numToken;
                    while (std::getline(numStream, numToken, ','))
                    {
                        numToken = trim(numToken);
                        if (!numToken.empty())
                        {
                            int value = std::stoi(numToken);
                            for (int coreId = 0; coreId < cores.size(); coreId++)
                            {
                                int baseAddress = coreId * SharedMemory::SEGMENT_SIZE;
                                sharedMemory->setWord(baseAddress + dataPointer, value);
                            }
                            dataPointer += 4;
                        }
                    }
                }
            }
        }
        else if (inTextSection)
        {
            size_t colonPos = processedLine.find(':');
            if (colonPos != std::string::npos)
            {
                std::string label = trim(processedLine.substr(0, colonPos));
                labelMap[label] = program.size();
                std::string rest = trim(processedLine.substr(colonPos + 1));
                if (!rest.empty())
                {
                    program.push_back(rest);
                }
            }
            else
            {
                program.push_back(processedLine);
            }
        }
    }

    for (auto &core : cores)
    {
        core.reset();
        core.setLabels(labelMap);
    }
}

void RISCVSimulator::barrierWait() {
    std::unique_lock<std::mutex> lock(barrierMutex);
    int currentGeneration = barrierGeneration;
    if (--barrierCount == 0) {
        barrierCount = cores.size();
        barrierGeneration++;
        barrierCV.notify_all();
    } else {
        barrierCV.wait(lock, [this, currentGeneration]() {
            return barrierGeneration != currentGeneration || !running;
        });
    }
}

void RISCVSimulator::run() {
    std::vector<std::thread> threads;
    running = true;
    barrierCount = cores.size();
    barrierGeneration = 0;

    for (auto &core : cores) {
        threads.emplace_back([&core, this]() {
            while (running && core.getPC() < program.size()) {
                barrierWait();
                if (!running)
                    break;

                bool exceptionOccurred = false;
                try {
                    const auto &instruction = program[core.getPC()];
                    if (instruction.find(':') == std::string::npos) {
                        core.executeInstruction(instruction);
                    }
                } catch (const std::exception &e) {
                    running = false;
                    barrierCV.notify_all();
                    exceptionOccurred = true;
                }
                barrierWait();
                if (!exceptionOccurred) {
                    core.incrementCycle();
                    // std::cout << "Cycle incremented: " << core.getCycleCount() << std::endl;
                }

                if (exceptionOccurred)
                    break;
            }
        });
    }
    for (auto &thread : threads) {
        thread.join();
    }
}

void RISCVSimulator::stop() {
    running = false;
}

void RISCVSimulator::printState() const {
    unsigned long totalCycles = 0;
    std::cout << "\n=== Final Simulator State ===\n";
    for (const auto& core : cores) {
        std::cout << "\n=== Core " << core.getCoreId() << " State ===\n";
        std::cout << "PC: 0x" << std::hex << std::setw(8) << std::setfill('0')
                  << core.getPC() << std::dec << "\n\n";
        std::cout << "Registers:\n";
        const auto& registers = core.getRegisters();
        for (size_t i = 0; i < registers.size(); i++) {
            std::cout << "x" << std::setw(2) << std::setfill('0') << i
                      << ": 0x" << std::hex << std::setw(8) << std::setfill('0')
                      << registers[i] << std::dec;
            if (i == 0) std::cout << " (zero)";
            if (i == 31) std::cout << " (core_id)";
            std::cout << "\n";
        }
        unsigned long coreCycles = core.getCycleCount();
        std::cout << "\nClock cycles for Core " << core.getCoreId() << ": " << coreCycles << "\n";
        if (coreCycles > totalCycles)
            totalCycles = coreCycles;
    }
    std::cout << "\nTotal clock cycles (global): " << totalCycles << "\n";

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
