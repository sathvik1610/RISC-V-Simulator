#ifndef CORE_HPP
#define CORE_HPP

#include <vector>
#include <string>
#include <memory>
#include <sstream>
#include <unordered_map>
#include "shared_memory.hpp"

class Core {
private:
    const int coreId;
    std::vector<int> registers;
    std::shared_ptr<SharedMemory> sharedMemory;
    int pc;
    static constexpr int NUM_REGISTERS = 32;
    std::unordered_map<std::string, int> labels;

    void executeArithmeticInstruction(const std::string& op, std::istringstream& iss);
    void executeMemoryInstruction(const std::string& op, std::istringstream& iss);
    void executeJumpInstruction(std::istringstream& iss);
    void executeBranchInstruction(std::istringstream& iss);
    int registerNumber(const std::string &regName);
    void executeBLTInstruction(std::istringstream& iss);
    unsigned long cycleCount;

    std::vector<std::string> parseOperands(const std::string& rest);

public:
    std::string trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\n\r");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\n\r");
        return str.substr(first, (last - first + 1));
    }
    std::string trimAndRemoveComments(const std::string& str) {
        std::string result = str;
        size_t commentPos = result.find('#');
        if (commentPos != std::string::npos) {
            result = result.substr(0, commentPos);
        }
        return trim(result);
    }

    Core(int id, std::shared_ptr<SharedMemory> memory);
    Core(const Core&) = delete;
    Core& operator=(const Core&) = delete;
    Core(Core&&) = default;
    Core& operator=(Core&&) = default;

    void reset();
    int getCoreId() const { return coreId; }
    int getPC() const { return pc; }
    int getRegister(int index) const;
    const std::vector<int>& getRegisters() const { return registers; }
    void setRegister(int index, int value);

    int loadWord(int address) const {
        return sharedMemory->loadWord(coreId, address);
    }
    void storeWord(int address, int value) {
        sharedMemory->storeWord(coreId, address, value);
    }

    void executeInstruction(const std::string& instruction);
    void collectLabels(const std::string& instruction, int position);
    void setLabels(const std::unordered_map<std::string, int>& lbls);
    const std::unordered_map<std::string, int>& getLabels() const;
    void resetCycleCount() { cycleCount = 0; }
    void incrementCycle() { cycleCount++; }
    unsigned long getCycleCount() const { return cycleCount; }
};

#endif
