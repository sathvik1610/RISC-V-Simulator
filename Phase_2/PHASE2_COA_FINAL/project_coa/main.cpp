#include "pipelined_simulator.hpp"
#include <iostream>
#include <string>
#include <limits>

std::string trim(const std::string &s) {
    size_t first = s.find_first_not_of(" \t\n\r");
    if (first == std::string::npos)
        return "";
    size_t last = s.find_last_not_of(" \t\n\r");
    return s.substr(first, last - first + 1);
}



void configureInstructionLatencies(PipelinedSimulator& simulator) {
    std::cout << "Configure instruction latencies? (y/n): ";
    std::string response;
    std::getline(std::cin, response);
    
    if (trim(response) == "y" || trim(response) == "Y") {
        std::vector<std::string> instructions = {"add", "addi", "sub", "slt", "mul"};
        
        for (const auto& instruction : instructions) {
            std::cout << "Enter latency for " << instruction << " (default is " 
                      << simulator.getInstructionLatency(instruction) << "): ";
            std::string latencyStr;
            std::getline(std::cin, latencyStr);
            latencyStr = trim(latencyStr);
            
            if (!latencyStr.empty()) {
                try {
                    int latency = std::stoi(latencyStr);
                    if (latency > 0) {
                        simulator.setInstructionLatency(instruction, latency);
                        std::cout << "Set " << instruction << " latency to " << latency << "\n";
                    } else {
                        std::cout << "Latency must be positive. Using default.\n";
                    }
                } catch (const std::exception& e) {
                    std::cout << "Invalid input. Using default.\n";
                }
            }
        }
    }
}

int main() {
    std::cout << "RISC-V Pipelined Multi-Core Simulator (Phase 2)\n\n";
    
    // Create simulator with 4 cores
    PipelinedSimulator simulator(4);
    // Configure forwarding
    std::cout << "Enable data forwarding? (y/n): ";
    std::string forwardingResponse;
    std::getline(std::cin, forwardingResponse);
    bool enableForwarding = (trim(forwardingResponse) == "y" || trim(forwardingResponse) == "Y");
    simulator.setForwardingEnabled(enableForwarding);
    
    // Configure instruction latencies
    configureInstructionLatencies(simulator);
    
    // Load program
    std::cout << "\nEnter the file name containing the assembly code: ";
    std::string filename;
    std::getline(std::cin, filename);
    filename = trim(filename);
    
    if (filename.empty()) {
        std::cerr << "No file name provided. Exiting.\n";
        return 1;
    }
    
    try {
        simulator.loadProgramFromFile(filename);
    } catch (const std::exception &e) {
        std::cerr << "Error loading program from file: " << e.what() << "\n";
        return 1;
    }
    
    // Run simulation
    std::cout << "\nRunning simulation...\n";
    simulator.run();
    
    // Print results
    // simulator.printState();
    // simulator.printStatistics();
    
    return 0;
}