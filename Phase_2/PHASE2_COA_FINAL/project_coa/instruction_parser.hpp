#ifndef INSTRUCTION_PARSER_HPP
#define INSTRUCTION_PARSER_HPP

#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <regex>
#include "pipeline.hpp"

class InstructionParser {
public:
    static Instruction parseInstruction(const std::string &raw, int coreId) {
        int id;
        Instruction inst;
        inst.raw = raw;
        inst.coreId = coreId;

        std::istringstream iss(raw);
        std::string opcode;
        iss >> opcode;
        inst.opcode = opcode;

        if (opcode == "add" || opcode == "sub" || opcode == "slt" || opcode == "mul") {
            inst.isArithmetic = true;
            parseRTypeInstruction(inst, iss);
        }
        else if (opcode == "addi") {
            inst.isArithmetic = true;
            parseITypeInstruction(inst, iss);
        }
        else if (opcode == "lw") {
            inst.isMemory = true;
            parseLoadInstruction(inst, iss);
        }
        else if (opcode == "sw") {
            inst.isMemory = true;
            parseStoreInstruction(inst, iss);
        }
        else if (opcode == "bne" || opcode == "blt" || opcode == "beq") {
            inst.isBranch = true;
            parseBranchInstruction(inst, iss);
        }
        else if (opcode == "jal") {
            inst.isJump = true;
            parseJumpInstruction(inst, iss);
        }
        else if (opcode == "la") {
            parseLoadAddressInstruction(inst, iss);
        }

        return inst;
    }

private:
    static void parseRTypeInstruction(Instruction &inst, std::istringstream &iss) {
        std::string rest;
        std::getline(iss, rest);
        auto operands = parseOperands(rest);

        if (operands.size() >= 3) {
            inst.rd = parseRegister(operands[0]);
            inst.rs1 = parseRegister(operands[1]);
            inst.rs2 = parseRegister(operands[2]);
        }
    }

    static void parseITypeInstruction(Instruction &inst, std::istringstream &iss) {
        std::string rest;
        std::getline(iss, rest);
        auto operands = parseOperands(rest);

        if (operands.size() >= 3) {
            inst.rd = parseRegister(operands[0]);
            inst.rs1 = parseRegister(operands[1]);
            inst.immediate = std::stoi(operands[2]);
        }
    }

    static void parseLoadInstruction(Instruction &inst, std::istringstream &iss) {
        std::string rest;
        std::getline(iss, rest);
        auto operands = parseOperands(rest);

        if (operands.size() >= 2) {
            inst.rd = parseRegister(operands[0]);

            std::string offsetBase = operands[1];
            size_t openParen = offsetBase.find('(');
            size_t closeParen = offsetBase.find(')');

            if (openParen != std::string::npos && closeParen != std::string::npos) {
                std::string offsetStr = offsetBase.substr(0, openParen);
                std::string baseReg = offsetBase.substr(openParen + 1, closeParen - openParen - 1);

                inst.immediate = offsetStr.empty() ? 0 : std::stoi(offsetStr);
                inst.rs1 = parseRegister(baseReg);
            }
        }
    }

    static void parseStoreInstruction(Instruction &inst, std::istringstream &iss) {
        std::string rest;
        std::getline(iss, rest);
        auto operands = parseOperands(rest);

        if (operands.size() >= 2) {
            inst.rs2 = parseRegister(operands[0]); 

            std::string offsetBase = operands[1];
            size_t openParen = offsetBase.find('(');
            size_t closeParen = offsetBase.find(')');

            if (openParen != std::string::npos && closeParen != std::string::npos) {
                std::string offsetStr = offsetBase.substr(0, openParen);
                std::string baseReg = offsetBase.substr(openParen + 1, closeParen - openParen - 1);

                inst.immediate = offsetStr.empty() ? 0 : std::stoi(offsetStr);
                inst.rs1 = parseRegister(baseReg);
            }
        }
    }

    static void parseBranchInstruction(Instruction &inst, std::istringstream &iss) {
        std::string rest;
        std::getline(iss, rest);
        auto operands = parseOperands(rest);
        if (operands.size() >= 3) {
            inst.rs1 = parseRegister(operands[0]);
            if (inst.opcode == "beq") {
                if (inst.rs1 == 31) {
                    inst.useCID = true;
                    inst.rs2 = std::stoi(operands[1]);
                }
                else {
                    inst.rs2 = parseRegister(operands[1]);
                }
            }
            else {
                inst.rs2 = parseRegister(operands[1]);
            }
            inst.label = operands[2];
            inst.targetPC = -1;
        }
    }

    static void parseJumpInstruction(Instruction &inst, std::istringstream &iss) {
        std::string rest;
        std::getline(iss, rest);
        rest = trim(rest); 
        auto operands = parseOperands(rest);
        if (operands.size() == 1) {
            
            inst.rd = -1; 
            std::string lab = operands[0];
            if (!lab.empty() && lab[0] == '.')
                lab = lab.substr(1);
            inst.label = lab;
            inst.targetPC = -1; 
            std::cout << "[InstructionParser] Parsed jal (no link) for \"" << inst.raw
                    << "\" as label=\"" << inst.label << "\"\n";
        }
        else if (operands.size() >= 2) {
            
            inst.rd = parseRegister(operands[0]);
            std::string lab = operands[1];
            if (!lab.empty() && lab[0] == '.')
                lab = lab.substr(1);
            inst.label = lab;
            inst.targetPC = -1; 
            std::cout << "[InstructionParser] Parsed jal for \"" << inst.raw
                    << "\" as rd=" << inst.rd << ", label=\"" << inst.label << "\"\n";
        }
        else {
            std::cerr << "[InstructionParser] Error: could not parse operands from \""
                    << rest << "\" in instruction \"" << inst.raw << "\"\n";
        }
    }

    static void parseLoadAddressInstruction(Instruction &inst, std::istringstream &iss) {
        std::string rest;
        std::getline(iss, rest);
        auto operands = parseOperands(rest);
        if (operands.size() >= 2) {
            inst.rd = parseRegister(operands[0]);
            std::string lab = operands[1];
            
            if (!lab.empty() && lab[0] == '.') {
                lab = lab.substr(1);
            }
            inst.label = lab;
            std::cout << "[InstructionParser] Parsed la label for \"" << inst.raw
                    << "\" as \"" << inst.label << "\"\n";
        }
    }

    static int parseRegister(const std::string &reg) {
        if (reg.size() >= 2 && reg[0] == 'x') {
            return std::stoi(reg.substr(1));
        }
        return -1;
    }

    static std::vector<std::string> parseOperands(const std::string &rest) {
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
            operand = trim(operand);
            if (!operand.empty()) {
                operands.push_back(operand);
            }
            ++iter;
        }

        return operands;
    }

    static std::string trim(const std::string &str) {
        size_t first = str.find_first_not_of(" \t\n\r");
        if (first == std::string::npos)
            return "";
        size_t last = str.find_last_not_of(" \t\n\r");
        return str.substr(first, (last - first + 1));
    }
};

#endif
