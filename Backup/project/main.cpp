#include "pipelined_simulator.hpp"
#include "memory_hierarchy.hpp"
#include "scratchpad.hpp"
#include "cache.hpp"
#include <iostream>
#include <string>
#include <limits>
#include <fstream>
#include <sstream>

// Trim whitespace from both ends of a string
std::string trim(const std::string &s) {
    size_t first = s.find_first_not_of(" \t\n\r");
    if (first == std::string::npos)
        return "";
    size_t last = s.find_last_not_of(" \t\n\r");
    return s.substr(first, last - first + 1);
}

// Read cache and memory parameters from a config file
bool readCacheConfig(const std::string& filename, 
    int& l1Size, int& l1BlockSize, int& l1Assoc, int& l1Latency,
    int& l2Size, int& l2BlockSize, int& l2Assoc, int& l2Latency,
    int& memLatency, int& spmSize, int& spmLatency) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Could not open cache configuration file: " << filename << "\n";
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string key, value;
        if (std::getline(iss, key, '=') && std::getline(iss, value)) {
            key = trim(key);
            value = trim(value);

            if (key == "L1_SIZE")           l1Size       = std::stoi(value);
            else if (key == "L1_BLOCK_SIZE") l1BlockSize  = std::stoi(value);
            else if (key == "L1_ASSOCIATIVITY") l1Assoc    = std::stoi(value);
            else if (key == "L1_LATENCY")    l1Latency    = std::stoi(value);
            else if (key == "L2_SIZE")       l2Size       = std::stoi(value);
            else if (key == "L2_BLOCK_SIZE") l2BlockSize  = std::stoi(value);
            else if (key == "L2_ASSOCIATIVITY") l2Assoc    = std::stoi(value);
            else if (key == "L2_LATENCY")    l2Latency    = std::stoi(value);
            else if (key == "MEMORY_LATENCY") memLatency   = std::stoi(value);
            else if (key == "SPM_SIZE")      spmSize      = std::stoi(value);
            else if (key == "SPM_LATENCY")   spmLatency   = std::stoi(value);
        }
    }
    return true;
}

// Allow the user to override per-instruction latencies
void configureInstructionLatencies(PipelinedSimulator& simulator) {
    std::cout << "Configure instruction latencies? (y/n): ";
    std::string response;
    std::getline(std::cin, response);
    response = trim(response);

    if (response == "y" || response == "Y") {
        std::vector<std::string> instructions = {"add", "addi", "sub", "slt", "mul"};
        for (const auto& inst : instructions) {
            int defaultLat = simulator.getInstructionLatency(inst);
            std::cout << "Enter latency for " << inst 
                      << " (default " << defaultLat << "): ";
            std::string latStr;
            std::getline(std::cin, latStr);
            latStr = trim(latStr);
            if (!latStr.empty()) {
                try {
                    int lat = std::stoi(latStr);
                    if (lat > 0) {
                        simulator.setInstructionLatency(inst, lat);
                        std::cout << "Set " << inst << " latency to " << lat << "\n";
                    } else {
                        std::cout << "Latency must be positive. Using default.\n";
                    }
                } catch (...) {
                    std::cout << "Invalid input. Using default.\n";
                }
            }
        }
    }
}

int main() {
    std::cout << "RISC-V Pipelined Multi-Core Simulator (Phase 3)\n\n";
    std::cerr << "[CANARY] Recompiled binary is running!\n";

    // Default cache and memory parameters
    int l1Size      = 1024;    // 1KB
    int l1BlockSize = 64;      // 64 bytes
    int l1Assoc     = 1;       // Direct mapped
    int l1Latency   = 1;       // 1 cycle
    int l2Size      = 4096;    // 4KB
    int l2BlockSize = 64;      // 64 bytes
    int l2Assoc     = 4;       // 4-way
    int l2Latency   = 5;       // 5 cycles
    int memLatency  = 100;     // 100 cycles
    int spmSize     = 1024;    // 1KB
    int spmLatency  = 1;       // 1 cycle

    // Prompt for cache config file
    std::cout << "Enter cache configuration file (leave empty for defaults): ";
    std::string cfg;
    std::getline(std::cin, cfg);
    cfg = trim(cfg);
    if (!cfg.empty()) {
        if (!readCacheConfig(cfg, l1Size, l1BlockSize, l1Assoc, l1Latency,
                             l2Size, l2BlockSize, l2Assoc, l2Latency,
                             memLatency, spmSize, spmLatency)) {
            std::cout << "Using default cache configuration.\n";
        }
    }

    // Display the chosen configuration
    std::cout << "\nCache Configuration:\n";
    std::cout << "L1: " << l1Size << "B, " << l1BlockSize << "B blocks, "
              << l1Assoc << "-way, " << l1Latency << " cycle(s)\n";
    std::cout << "L2: " << l2Size << "B, " << l2BlockSize << "B blocks, "
              << l2Assoc << "-way, " << l2Latency << " cycle(s)\n";
    std::cout << "Main Mem Latency: " << memLatency << " cycle(s)\n";
    std::cout << "SPM: " << spmSize << "B, " << spmLatency << " cycle(s)\n\n";

    try {
        // Instantiate simulator with 4 cores
        PipelinedSimulator sim(
            4,
            l1Size, l1BlockSize, l1Assoc, l1Latency,
            l2Size, l2BlockSize, l2Assoc, l2Latency,
            memLatency, spmSize, spmLatency
        );

        // Data forwarding option
        std::cout << "Enable data forwarding? (y/n): ";
        std::string fwd;
        std::getline(std::cin, fwd);
        bool forwardEnable = (trim(fwd) == "y" || trim(fwd) == "Y");
        sim.setForwardingEnabled(forwardEnable);

        // Replacement policy selection
        std::cout << "Select replacement policy:\n"
                  << "1. LRU\n2. FIFO\nChoice (1/2): ";
        std::string rp;
        std::getline(std::cin, rp);
        Cache::Policy repl = (trim(rp) == "2" ? Cache::Policy::FIFO : Cache::Policy::LRU);
        sim.setCachePolicy(repl);
        std::cout << ((repl == Cache::Policy::LRU) ? "Using LRU\n" : "Using FIFO\n");

        // Write policy and write-allocate (extra features from original)
        std::cout << "Write policy (1=WB,2=WT, default WB): ";
        std::string wp;
        std::getline(std::cin, wp);
        if (trim(wp) == "2") sim.setWritePolicy(WritePolicy::WRITE_THROUGH);
        else sim.setWritePolicy(WritePolicy::WRITE_BACK);
        

        std::cout << "Write-allocate? (1=WA,2=NWA, default WA): ";
        std::string wa;
        std::getline(std::cin, wa);
        if (trim(wa) == "2") sim.setWriteAllocatePolicy(WriteAllocatePolicy::NO_WRITE_ALLOCATE);
        else sim.setWriteAllocatePolicy(WriteAllocatePolicy::WRITE_ALLOCATE);

        // Allow user to tweak instruction latencies
        configureInstructionLatencies(sim);

        // Program loading
        std::cout << "\nEnter assembly file to load: ";
        std::string asmfile;
        std::getline(std::cin, asmfile);
        if (asmfile.empty()) {
            std::cerr << "No file provided. Exiting.\n";
            return 1;
        }
        sim.loadProgramFromFile(asmfile);

        // Run and finish
        std::cout << "\nRunning simulation...\n";
        sim.run();
        std::cout << "Simulation completed.\n";

    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
