#include "simulator.hpp"
#include <iostream>
#include <string>

std::string trim2(const std::string &s) {
    size_t first = s.find_first_not_of(" \t\n\r");
    if (first == std::string::npos)
        return "";
    size_t last = s.find_last_not_of(" \t\n\r");
    return s.substr(first, last - first + 1);
}

int main()
{
    // std::cout << "RISC-V Multi-Core Simulator\n";
    // std::cout << "Enter your assembly code (type 'END' on a new line to finish):\n";
    //
    // std::string line;
    // std::vector<std::string> lines;
    //
    // // Read until the user types "END"
    // while (std::getline(std::cin, line))
    // {
    //     if (trim2(line) == "END")
    //         break;
    //     lines.push_back(line);
    // }
    //
    // // Join the lines into a single string.
    // std::string program;
    // for (const auto &l : lines)
    // {
    //     program += l + "\n";
    // }

    std::cout << "Enter the file name containing the assembly code: ";
    std::string filename;
    std::getline(std::cin, filename);
    filename = trim2(filename);
    if (filename.empty()) {
        std::cerr << "No file name provided. Exiting.\n";
        return 1;
    }
    
    RISCVSimulator simulator(4);
    
    try {
        simulator.loadProgramFromFile(filename);
    }
    catch (const std::exception &e) {
        std::cerr << "Error loading program from file: " << e.what() << "\n";
        return 1;
    }
    
    simulator.run();
    simulator.printState();
    
    return 0;
}
