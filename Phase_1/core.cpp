#include "core.hpp"
#include <sstream>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <algorithm>
#include <unordered_map>
#include "debug.h"

Core::Core(int id, std::shared_ptr<SharedMemory> memory)
    : coreId(id)
    , registers(NUM_REGISTERS, 0)
    , sharedMemory(memory)
    , pc(0)
 , cycleCount(0)

{
    registers[31] = coreId;
}

void Core::reset() {
    std::fill(registers.begin(), registers.end(), 0);
    registers[31] = coreId;
    pc = 0;
    labels.clear();
    resetCycleCount();
}

int Core::getRegister(int index) const {
    if (index < 0 || index >= NUM_REGISTERS) {
        throw std::out_of_range("Register index out of range");
    }
    if (index == 31) {
        return coreId;
    }
    return registers[index];
}

int Core::registerNumber(const std::string &regName) {
    if (regName.size() >= 2 && regName[0] == 'x') {
        return std::stoi(regName.substr(1));
    }
    throw std::runtime_error("Unknown register: " + regName);
}

void Core::setRegister(int index, int value){
    if (index < 0 || index >= NUM_REGISTERS){
        throw std::out_of_range("Register index out of range");
    }
    if (index != 0 && index != 31){
        registers[index] = value;
    }
}

void Core::executeInstruction(const std::string& instruction) {
    std::istringstream iss(instruction);
    std::string op;
    iss >> op;

    if (op == "halt") {
        throw std::runtime_error("Halt instruction encountered");
    }

    if (!op.empty() && op.back() == ':') {
        return;
    }

    try {
        if (op == "add" || op == "addi" || op == "sub" || op == "slt" || op == "mul") {
            executeArithmeticInstruction(op, iss);
        }
        else if (op == "lw" || op == "sw") {
            executeMemoryInstruction(op, iss);
        }
        else if (op == "jal") {
            executeJumpInstruction(iss);
            return;
        }
        else if (op == "bne") {
            executeBranchInstruction(iss);
            return;
        }
        else if (op == "blt") {
            executeBLTInstruction(iss);
            return;
        }
        else if(op=="la"){
            std::string rdStr, label;
            iss >> rdStr >> label;
            if (!rdStr.empty() && rdStr.back() == ',') {
                rdStr.pop_back();
            }
            int rd = registerNumber(rdStr);

            if (labels.find(label) == labels.end()) {
                throw std::runtime_error("Label not found: " + label);
            }
            int relativeOffset = labels[label];  // The offset stored from the data section
            int effectiveAddress = coreId * SharedMemory::SEGMENT_SIZE + relativeOffset;

            registers[rd] = effectiveAddress;

            pc++;
            return;
        }

        else {
            throw std::runtime_error("Unknown instruction: " + op);
        }
        pc++;
    }
    catch (const std::exception& e) {
        std::string errorMsg = "Error executing instruction '" + instruction + "': " + e.what();
        throw std::runtime_error(errorMsg);
    }
}

void Core::executeArithmeticInstruction(const std::string& op, std::istringstream& iss) {
    std::string rest;
    std::getline(iss, rest);
    std::vector<std::string> operands = parseOperands(rest);

    if (operands.size() < 3) {
        throw std::runtime_error("Not enough operands for arithmetic instruction");
    }

    if (op == "add") {
        int rd_num = std::stoi(operands[0].substr(1));
        int rs1_num = std::stoi(operands[1].substr(1));
        int rs2_num = std::stoi(operands[2].substr(1));
        int result = getRegister(rs1_num) + getRegister(rs2_num);
        setRegister(rd_num, result);
    }
    else if (op == "addi") {
        int rd_num = std::stoi(operands[0].substr(1));
        int rs1_num = std::stoi(operands[1].substr(1));
        int imm = std::stoi(operands[2]);
        int result = getRegister(rs1_num) + imm;
        setRegister(rd_num, result);
    }
    else if (op == "sub") {
        int rd_num = std::stoi(operands[0].substr(1));
        int rs1_num = std::stoi(operands[1].substr(1));
        int rs2_num = std::stoi(operands[2].substr(1));
        int result = getRegister(rs1_num) - getRegister(rs2_num);
        setRegister(rd_num, result);
    }
    else if (op == "slt") {
        int rd_num = std::stoi(operands[0].substr(1));
        int rs1_num = std::stoi(operands[1].substr(1));
        int rs2_num = std::stoi(operands[2].substr(1));
        int result = (getRegister(rs1_num) < getRegister(rs2_num)) ? 1 : 0;
        setRegister(rd_num, result);
    }
    else if (op == "mul") {
        int rd_num = std::stoi(operands[0].substr(1));
        int rs1_num = std::stoi(operands[1].substr(1));
        int rs2_num = std::stoi(operands[2].substr(1));
        int result = getRegister(rs1_num) * getRegister(rs2_num);
        setRegister(rd_num, result);
    }
}

void Core::executeMemoryInstruction(const std::string& op, std::istringstream& iss) {
    std::string rest;
    std::getline(iss, rest);
    std::vector<std::string> parts = parseOperands(rest);

    if (parts.size() < 2) {
        throw std::runtime_error("Not enough operands for memory instruction");
    }

    std::string offsetBase = parts[1];
    size_t openParen = offsetBase.find('(');
    size_t closeParen = offsetBase.find(')');

    if (openParen == std::string::npos || closeParen == std::string::npos) {
        throw std::runtime_error("Invalid memory access format");
    }

    std::string offsetStr = offsetBase.substr(0, openParen);
    std::string baseReg = offsetBase.substr(openParen + 1, closeParen - openParen - 1);
    int offset = offsetStr.empty() ? 0 : std::stoi(offsetStr);
    int baseRegNum = std::stoi(baseReg.substr(1));
    int effectiveAddress = getRegister(baseRegNum) + offset;

    const int segmentSizeBytes = 1024;
    int segmentStart = coreId * segmentSizeBytes;
    int segmentEnd = (coreId + 1) * segmentSizeBytes - 4;

    if (effectiveAddress < segmentStart || effectiveAddress > segmentEnd) {
        if (op == "lw") {
            int rd_num = std::stoi(parts[0].substr(1));
            setRegister(rd_num, 0);
        }
        return;
    }

    if (op == "lw") {
        int rd_num = std::stoi(parts[0].substr(1));
        int loadedValue = sharedMemory->loadWord(coreId, effectiveAddress);
        setRegister(rd_num, loadedValue);
    } else { // sw
        int rs2_num = std::stoi(parts[0].substr(1));
        int value = getRegister(rs2_num);
        sharedMemory->storeWord(coreId, effectiveAddress, value);
    }
}

void Core::executeJumpInstruction(std::istringstream& iss) {
    std::string rd, label;
    iss >> rd >> label;
    int rd_num = std::stoi(rd.substr(1));
    int returnAddress = pc + 1;
    setRegister(rd_num, returnAddress);
    auto it = labels.find(label);
    if (it != labels.end()) {
        pc = it->second;
    }
    else {
        throw std::runtime_error("Undefined label: " + label);
    }
}

void Core::executeBranchInstruction(std::istringstream& iss) {
    std::string rest;
    std::getline(iss, rest);
    auto operands = parseOperands(rest);
    if (operands.size() < 3) {
        throw std::runtime_error("Not enough operands for branch instruction");
    }
    int rs1_num = std::stoi(operands[0].substr(1));
    int rs2_num = std::stoi(operands[1].substr(1));
    std::string label = trim(operands[2]);
    if (getRegister(rs1_num) != getRegister(rs2_num)) {
        auto it = labels.find(label);
        if (it == labels.end()) {
            throw std::runtime_error("Undefined label: " + label);
        }
        pc = it->second;
    } else {
        pc++;
    }
}

void Core::executeBLTInstruction(std::istringstream &iss) {
    std::string rest;
    std::getline(iss, rest);
    auto operands = parseOperands(rest);
    if (operands.size() < 3) {
        throw std::runtime_error("Not enough operands for blt instruction");
    }
    int rs1_num = std::stoi(operands[0].substr(1));
    int rs2_num = std::stoi(operands[1].substr(1));
    std::string label = trim(operands[2]);
    if (getRegister(rs1_num) < getRegister(rs2_num)) {
        auto it = labels.find(label);
        if (it == labels.end()) {
            throw std::runtime_error("Undefined label: " + label);
        }
        pc = it->second;
    } else {
        pc++;
    }
}

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

std::vector<std::string> Core::parseOperands(const std::string& rest) {
    std::string operandsStr = rest;
    size_t commentPos = operandsStr.find('#');
    if (commentPos != std::string::npos) {
        operandsStr = operandsStr.substr(0, commentPos);
    }
    std::vector<std::string> operands;
    std::regex reg(",\\s*");
    std::sregex_token_iterator iter(operandsStr.begin(), operandsStr.end(), reg, -1);
    std::sregex_token_iterator end;
    while (iter != end) {
        std::string operand = iter->str();
        operand.erase(0, operand.find_first_not_of(" \t"));
        operand.erase(operand.find_last_not_of(" \t") + 1);
        if (!operand.empty()) {
            operands.push_back(operand);
        }
        ++iter;
    }
    return operands;
}

void Core::collectLabels(const std::string& instruction, int position) {
    if (instruction.find(':') != std::string::npos) {
        std::string label = instruction.substr(0, instruction.find(':'));
        label.erase(0, label.find_first_not_of(" \t"));
        label.erase(label.find_last_not_of(" \t") + 1);
        labels[label] = position;
    }
}

void Core::setLabels(const std::unordered_map<std::string, int>& lbls) {
    labels = lbls;
}

const std::unordered_map<std::string, int>& Core::getLabels() const {
    return labels;
}

